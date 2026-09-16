#pragma once

#include "emberdb/catalog/schema.h"
#include "emberdb/storage/disk/disk_manager.h"
#include "emberdb/storage/buffer/buffer_pool_manager.h"
#include "emberdb/storage/table/table.h"
#include "emberdb/index/index_info.h"
#include <string>
#include <unordered_map>
#include <memory>
#include <vector>
#include "emberdb/common/rw_latch.h"

namespace emberdb {

constexpr page_id_t CATALOG_PAGE_ID = 0;

/**
 * Catalog manages metadata for all tables and indexes in EmberDB,
 * persisting schema and index definitions to disk page 0.
 */
class Catalog {
public:
    explicit Catalog(BufferPoolManager* bpm);
    explicit Catalog(DiskManager* disk_mgr);

    Status Init();

    // Table operations
    Result<Table*> CreateTable(const std::string& name, const Schema& schema);
    Table* GetTable(const std::string& name) const;
    bool HasTable(const std::string& name) const;
    std::vector<std::string> GetAllTableNames() const;

    // Index operations
    Result<IndexInfo*> CreateIndex(const std::string& index_name, const std::string& table_name, const std::string& column_name);
    IndexInfo* GetIndex(const std::string& index_name) const;
    std::vector<IndexInfo*> GetTableIndexes(const std::string& table_name) const;
    bool HasIndex(const std::string& index_name) const;
    std::vector<std::string> GetAllIndexNames() const;

    Status PersistCatalog();

    BufferPoolManager* GetBufferPoolManager() const { return bpm_; }
    DiskManager* GetDiskManager() const { return disk_mgr_; }

private:
    Status LoadCatalog();
    Status PersistCatalogUnlocked();

    std::unique_ptr<BufferPoolManager> owned_bpm_;
    BufferPoolManager* bpm_;
    DiskManager* disk_mgr_;
    std::unordered_map<std::string, std::unique_ptr<Table>> tables_;
    std::vector<std::string> table_names_;
    std::unordered_map<std::string, std::unique_ptr<IndexInfo>> indexes_;
    std::vector<std::string> index_names_;
    mutable ReaderWriterLatch catalog_latch_;
};

} // namespace emberdb
