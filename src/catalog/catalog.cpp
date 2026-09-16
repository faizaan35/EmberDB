#include "emberdb/catalog/catalog.h"
#include <cstring>

namespace emberdb {

static constexpr char CATALOG_MAGIC[16] = "EMBERDB_CATALOG";

Catalog::Catalog(BufferPoolManager* bpm)
    : bpm_(bpm), disk_mgr_(bpm->GetDiskManager()) {}

Catalog::Catalog(DiskManager* disk_mgr)
    : owned_bpm_(std::make_unique<BufferPoolManager>(64, disk_mgr)),
      bpm_(owned_bpm_.get()),
      disk_mgr_(disk_mgr) {}

Status Catalog::Init() {
    if (!disk_mgr_->IsOpen()) {
        return Status::IOError("Disk manager is not open");
    }

    if (disk_mgr_->GetNumPages() == 0) {
        // First initialization: allocate page 0 for catalog
        page_id_t cat_pid = INVALID_PAGE_ID;
        Page* p = bpm_->NewPage(&cat_pid);
        if (!p) {
            return Status::IOError("Failed to allocate catalog page");
        }
        if (cat_pid != CATALOG_PAGE_ID) {
            bpm_->UnpinPage(cat_pid, false);
            return Status::Corruption("Catalog page is not page 0");
        }
        bpm_->UnpinPage(cat_pid, true);
        return PersistCatalog();
    }

    // Load existing catalog from page 0
    return LoadCatalog();
}

Status Catalog::PersistCatalog() {
    Page* page = bpm_->FetchPage(CATALOG_PAGE_ID);
    if (!page) {
        return Status::IOError("Failed to fetch catalog page from buffer pool");
    }

    char* dest = page->GetData();
    std::memset(dest, 0, PAGE_SIZE);

    // Write magic header
    std::memcpy(dest, CATALOG_MAGIC, sizeof(CATALOG_MAGIC));
    dest += sizeof(CATALOG_MAGIC);

    // Write table count
    uint32_t count = static_cast<uint32_t>(table_names_.size());
    std::memcpy(dest, &count, sizeof(count));
    dest += sizeof(count);

    for (const auto& name : table_names_) {
        const auto& tbl = tables_.at(name);

        uint16_t name_len = static_cast<uint16_t>(name.size());
        std::memcpy(dest, &name_len, sizeof(name_len));
        dest += sizeof(name_len);

        std::memcpy(dest, name.data(), name_len);
        dest += name_len;

        page_id_t first_p = tbl->GetTableHeap()->GetFirstPageId();
        std::memcpy(dest, &first_p, sizeof(first_p));
        dest += sizeof(first_p);

        tbl->GetSchema().SerializeTo(dest);
        dest += tbl->GetSchema().GetSerializedSize();
    }

    // Write index count
    uint32_t index_count = static_cast<uint32_t>(index_names_.size());
    std::memcpy(dest, &index_count, sizeof(index_count));
    dest += sizeof(index_count);

    for (const auto& iname : index_names_) {
        const auto& idx_info = indexes_.at(iname);

        uint16_t iname_len = static_cast<uint16_t>(iname.size());
        std::memcpy(dest, &iname_len, sizeof(iname_len));
        dest += sizeof(iname_len);
        std::memcpy(dest, iname.data(), iname_len);
        dest += iname_len;

        const auto& tname = idx_info->GetTableName();
        uint16_t tname_len = static_cast<uint16_t>(tname.size());
        std::memcpy(dest, &tname_len, sizeof(tname_len));
        dest += sizeof(tname_len);
        std::memcpy(dest, tname.data(), tname_len);
        dest += tname_len;

        const auto& cname = idx_info->GetColumnName();
        uint16_t cname_len = static_cast<uint16_t>(cname.size());
        std::memcpy(dest, &cname_len, sizeof(cname_len));
        dest += sizeof(cname_len);
        std::memcpy(dest, cname.data(), cname_len);
        dest += cname_len;

        uint32_t col_idx = idx_info->GetColumnIdx();
        std::memcpy(dest, &col_idx, sizeof(col_idx));
        dest += sizeof(col_idx);

        TypeId ktype = idx_info->GetKeyType();
        std::memcpy(dest, &ktype, sizeof(ktype));
        dest += sizeof(ktype);

        page_id_t root_pid = idx_info->GetIndex()->GetRootPageId();
        std::memcpy(dest, &root_pid, sizeof(root_pid));
        dest += sizeof(root_pid);
    }

    bpm_->UnpinPage(CATALOG_PAGE_ID, true);
    bpm_->FlushPage(CATALOG_PAGE_ID);
    return Status::OK();
}

Status Catalog::LoadCatalog() {
    Page* page = bpm_->FetchPage(CATALOG_PAGE_ID);
    if (!page) {
        return Status::IOError("Failed to fetch catalog page from buffer pool");
    }

    const char* src = page->GetData();

    if (std::memcmp(src, CATALOG_MAGIC, sizeof(CATALOG_MAGIC)) != 0) {
        bpm_->UnpinPage(CATALOG_PAGE_ID, false);
        return Status::Corruption("Invalid catalog page magic header");
    }
    src += sizeof(CATALOG_MAGIC);

    uint32_t count = 0;
    std::memcpy(&count, src, sizeof(count));
    src += sizeof(count);

    tables_.clear();
    table_names_.clear();
    indexes_.clear();
    index_names_.clear();

    for (uint32_t i = 0; i < count; ++i) {
        uint16_t name_len = 0;
        std::memcpy(&name_len, src, sizeof(name_len));
        src += sizeof(name_len);

        std::string name(src, name_len);
        src += name_len;

        page_id_t first_p = INVALID_PAGE_ID;
        std::memcpy(&first_p, src, sizeof(first_p));
        src += sizeof(first_p);

        size_t schema_bytes = 0;
        Schema schema = Schema::DeserializeFrom(src, schema_bytes);
        src += schema_bytes;

        auto table_heap = std::make_unique<TableHeap>(bpm_, first_p);
        table_heap->SetOnFirstPageAllocated([this](page_id_t) {
            PersistCatalog();
        });
        tables_[name] = std::make_unique<Table>(name, std::move(schema), std::move(table_heap));
        table_names_.push_back(name);
    }

    // Load indexes if present in catalog page
    if (src + sizeof(uint32_t) <= page->GetData() + PAGE_SIZE) {
        uint32_t index_count = 0;
        std::memcpy(&index_count, src, sizeof(index_count));
        src += sizeof(index_count);

        for (uint32_t i = 0; i < index_count; ++i) {
            uint16_t iname_len = 0;
            std::memcpy(&iname_len, src, sizeof(iname_len));
            src += sizeof(iname_len);
            std::string iname(src, iname_len);
            src += iname_len;

            uint16_t tname_len = 0;
            std::memcpy(&tname_len, src, sizeof(tname_len));
            src += sizeof(tname_len);
            std::string tname(src, tname_len);
            src += tname_len;

            uint16_t cname_len = 0;
            std::memcpy(&cname_len, src, sizeof(cname_len));
            src += sizeof(cname_len);
            std::string cname(src, cname_len);
            src += cname_len;

            uint32_t col_idx = 0;
            std::memcpy(&col_idx, src, sizeof(col_idx));
            src += sizeof(col_idx);

            TypeId ktype = TypeId::INVALID;
            std::memcpy(&ktype, src, sizeof(ktype));
            src += sizeof(ktype);

            page_id_t root_pid = INVALID_PAGE_ID;
            std::memcpy(&root_pid, src, sizeof(root_pid));
            src += sizeof(root_pid);

            auto btree = std::make_unique<BPlusTreeIndex>(iname, bpm_, ktype, root_pid);
            btree->SetOnRootPageChange([this](page_id_t) {
                PersistCatalog();
            });

            auto idx_info = std::make_unique<IndexInfo>(iname, tname, cname, col_idx, ktype, std::move(btree));
            indexes_[iname] = std::move(idx_info);
            index_names_.push_back(iname);
        }
    }

    bpm_->UnpinPage(CATALOG_PAGE_ID, false);
    return Status::OK();
}

Result<Table*> Catalog::CreateTable(const std::string& name, const Schema& schema) {
    if (HasTable(name)) {
        return Status::AlreadyExists("Table already exists: " + name);
    }

    auto table_heap = std::make_unique<TableHeap>(bpm_, INVALID_PAGE_ID);
    table_heap->SetOnFirstPageAllocated([this](page_id_t) {
        PersistCatalog();
    });
    auto table = std::make_unique<Table>(name, schema, std::move(table_heap));
    Table* table_ptr = table.get();

    tables_[name] = std::move(table);
    table_names_.push_back(name);

    auto status = PersistCatalog();
    if (!status.ok()) {
        tables_.erase(name);
        table_names_.pop_back();
        return status;
    }

    return table_ptr;
}

Table* Catalog::GetTable(const std::string& name) const {
    auto it = tables_.find(name);
    if (it == tables_.end()) return nullptr;
    return it->second.get();
}

bool Catalog::HasTable(const std::string& name) const {
    return tables_.find(name) != tables_.end();
}

std::vector<std::string> Catalog::GetAllTableNames() const {
    return table_names_;
}

Result<IndexInfo*> Catalog::CreateIndex(const std::string& index_name, const std::string& table_name, const std::string& column_name) {
    if (HasIndex(index_name)) {
        return Status::AlreadyExists("Index already exists: " + index_name);
    }
    Table* table = GetTable(table_name);
    if (!table) {
        return Status::NotFound("Table not found: " + table_name);
    }
    int col_idx = table->GetSchema().GetColIdx(column_name);
    if (col_idx < 0) {
        return Status::NotFound("Column not found in table schema: " + column_name);
    }
    TypeId key_type = table->GetSchema().GetColumn(static_cast<size_t>(col_idx)).GetType();

    auto btree = std::make_unique<BPlusTreeIndex>(index_name, bpm_, key_type, INVALID_PAGE_ID);
    btree->SetOnRootPageChange([this](page_id_t) {
        PersistCatalog();
    });

    auto idx_info = std::make_unique<IndexInfo>(index_name, table_name, column_name, static_cast<uint32_t>(col_idx), key_type, std::move(btree));
    IndexInfo* ptr = idx_info.get();

    indexes_[index_name] = std::move(idx_info);
    index_names_.push_back(index_name);

    auto status = PersistCatalog();
    if (!status.ok()) {
        indexes_.erase(index_name);
        index_names_.pop_back();
        return status;
    }

    return ptr;
}

IndexInfo* Catalog::GetIndex(const std::string& index_name) const {
    auto it = indexes_.find(index_name);
    if (it == indexes_.end()) return nullptr;
    return it->second.get();
}

std::vector<IndexInfo*> Catalog::GetTableIndexes(const std::string& table_name) const {
    std::vector<IndexInfo*> res;
    for (const auto& name : index_names_) {
        auto* idx = indexes_.at(name).get();
        if (idx->GetTableName() == table_name) {
            res.push_back(idx);
        }
    }
    return res;
}

bool Catalog::HasIndex(const std::string& index_name) const {
    return indexes_.find(index_name) != indexes_.end();
}

std::vector<std::string> Catalog::GetAllIndexNames() const {
    return index_names_;
}

} // namespace emberdb
