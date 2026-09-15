#pragma once

#include "forgedb/catalog/schema.h"
#include "forgedb/storage/disk/disk_manager.h"
#include "forgedb/storage/buffer/buffer_pool_manager.h"
#include "forgedb/storage/table/table.h"
#include <string>
#include <unordered_map>
#include <memory>
#include <vector>

namespace forgedb {

constexpr page_id_t CATALOG_PAGE_ID = 0;

/**
 * Catalog manages metadata for all tables in ForgeDB, persisting table definitions to disk page 0.
 */
class Catalog {
public:
    explicit Catalog(BufferPoolManager* bpm);
    explicit Catalog(DiskManager* disk_mgr);

    Status Init();

    Result<Table*> CreateTable(const std::string& name, const Schema& schema);
    Table* GetTable(const std::string& name) const;
    bool HasTable(const std::string& name) const;
    std::vector<std::string> GetAllTableNames() const;

    Status PersistCatalog();

    BufferPoolManager* GetBufferPoolManager() const { return bpm_; }
    DiskManager* GetDiskManager() const { return disk_mgr_; }

private:
    Status LoadCatalog();

    std::unique_ptr<BufferPoolManager> owned_bpm_;
    BufferPoolManager* bpm_;
    DiskManager* disk_mgr_;
    std::unordered_map<std::string, std::unique_ptr<Table>> tables_;
    std::vector<std::string> table_names_;
};

} // namespace forgedb
