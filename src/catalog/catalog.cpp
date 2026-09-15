#include "forgedb/catalog/catalog.h"
#include <cstring>

namespace forgedb {

static constexpr char CATALOG_MAGIC[16] = "FORGEDB_CATALOG";

Catalog::Catalog(DiskManager* disk_mgr) : disk_mgr_(disk_mgr) {}

Status Catalog::Init() {
    if (!disk_mgr_->IsOpen()) {
        return Status::IOError("Disk manager is not open");
    }

    if (disk_mgr_->GetNumPages() == 0) {
        // First initialization: allocate page 0 for catalog
        auto alloc_res = disk_mgr_->AllocatePage();
        if (!alloc_res.ok()) {
            return alloc_res.status();
        }
        if (*alloc_res != CATALOG_PAGE_ID) {
            return Status::Corruption("Catalog page is not page 0");
        }
        return PersistCatalog();
    }

    // Load existing catalog from page 0
    return LoadCatalog();
}

Status Catalog::PersistCatalog() {
    std::vector<char> page_buf(PAGE_SIZE, 0);
    char* dest = page_buf.data();

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

        page_id_t first_p = tbl->GetFirstPageId();
        std::memcpy(dest, &first_p, sizeof(first_p));
        dest += sizeof(first_p);

        tbl->GetSchema().SerializeTo(dest);
        dest += tbl->GetSchema().GetSerializedSize();
    }

    auto status = disk_mgr_->WritePage(CATALOG_PAGE_ID, page_buf.data());
    if (!status.ok()) return status;
    return disk_mgr_->Flush();
}

Status Catalog::LoadCatalog() {
    std::vector<char> page_buf(PAGE_SIZE);
    auto status = disk_mgr_->ReadPage(CATALOG_PAGE_ID, page_buf.data());
    if (!status.ok()) return status;

    const char* src = page_buf.data();

    // Check magic
    if (std::memcmp(src, CATALOG_MAGIC, sizeof(CATALOG_MAGIC)) != 0) {
        return Status::Corruption("Invalid catalog page header magic");
    }
    src += sizeof(CATALOG_MAGIC);

    uint32_t count = 0;
    std::memcpy(&count, src, sizeof(count));
    src += sizeof(count);

    tables_.clear();
    table_names_.clear();

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

        auto table_heap = std::make_unique<TableHeap>(disk_mgr_, first_p);
        table_heap->SetOnFirstPageAllocated([this](page_id_t) {
            PersistCatalog();
        });
        tables_[name] = std::make_unique<Table>(name, std::move(schema), std::move(table_heap));
        table_names_.push_back(name);
    }

    return Status::OK();
}

Result<Table*> Catalog::CreateTable(const std::string& name, const Schema& schema) {
    if (HasTable(name)) {
        return Status::AlreadyExists("Table already exists: " + name);
    }

    auto table_heap = std::make_unique<TableHeap>(disk_mgr_, INVALID_PAGE_ID);
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

} // namespace forgedb
