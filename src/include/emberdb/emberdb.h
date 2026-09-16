#pragma once

#include "emberdb/common/config.h"
#include "emberdb/common/status.h"
#include "emberdb/storage/disk/disk_manager.h"
#include "emberdb/storage/buffer/buffer_pool_manager.h"
#include "emberdb/catalog/catalog.h"
#include "emberdb/recovery/log_manager.h"
#include "emberdb/recovery/recovery_manager.h"
#include "emberdb/execution/executor/execution_engine.h"
#include <string>
#include <memory>

namespace emberdb {

/**
 * Main database instance interface.
 * Coordinates storage, catalog, execution, transactions, write-ahead logging, and crash recovery.
 */
class EmberDBInstance {
public:
    explicit EmberDBInstance(std::string db_directory = "data", size_t buffer_pool_size = 64);
    ~EmberDBInstance();

    Status Open();
    Status Close();
    void SimulateCrash();
    bool IsOpen() const { return is_open_; }

    QueryResult ExecuteQuery(const std::string& sql);

    Catalog* GetCatalog() { return catalog_.get(); }
    ExecutionEngine* GetExecutionEngine() { return engine_.get(); }
    BufferPoolManager* GetBufferPoolManager() { return bpm_.get(); }
    LogManager* GetLogManager() { return log_mgr_.get(); }
    RecoveryManager* GetRecoveryManager() { return recovery_mgr_.get(); }

    const std::string& GetDbDirectory() const { return db_directory_; }

private:
    std::string db_directory_;
    size_t buffer_pool_size_;
    bool is_open_{false};

    std::unique_ptr<DiskManager> disk_mgr_;
    std::unique_ptr<BufferPoolManager> bpm_;
    std::unique_ptr<LogManager> log_mgr_;
    std::unique_ptr<Catalog> catalog_;
    std::unique_ptr<RecoveryManager> recovery_mgr_;
    std::unique_ptr<ExecutionEngine> engine_;
};

} // namespace emberdb
