#pragma once

#include "emberdb/common/config.h"
#include "emberdb/common/status.h"
#include "emberdb/recovery/log_manager.h"
#include "emberdb/recovery/log_record.h"
#include "emberdb/storage/buffer/buffer_pool_manager.h"
#include "emberdb/storage/disk/disk_manager.h"
#include "emberdb/catalog/catalog.h"
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

namespace emberdb {

enum class ShutdownStatus {
    CLEAN,
    UNCLEAN
};

struct RecoveryStats {
    bool required{false};
    ShutdownStatus shutdown_status{ShutdownStatus::CLEAN};
    size_t total_log_records{0};
    size_t active_txns_count{0};
    size_t committed_txns_count{0};
    size_t redone_records{0};
    size_t undone_records{0};
};

/**
 * RecoveryManager implements crash recovery following the ARIES-style
 * Analysis, Redo, and Undo passes over the write-ahead log.
 */
class RecoveryManager {
public:
    RecoveryManager(DiskManager* disk_mgr,
                    BufferPoolManager* bpm,
                    Catalog* catalog,
                    LogManager* log_mgr);
    ~RecoveryManager() = default;

    // Inspect WAL to determine if recovery is required
    bool NeedsRecovery();

    // Execute full recovery sequence: Analysis -> Redo -> Undo
    Result<RecoveryStats> Recover();

    // Record a clean shutdown checkpoint in WAL
    Status RecordCleanShutdown();

private:
    void AnalysisPass(const std::vector<LogRecord>& records,
                      std::unordered_set<txn_id_t>& active_txns,
                      std::unordered_set<txn_id_t>& committed_txns,
                      lsn_t& max_lsn,
                      bool& saw_clean_shutdown);

    size_t RedoPass(const std::vector<LogRecord>& records);

    size_t UndoPass(const std::vector<LogRecord>& records,
                    const std::unordered_set<txn_id_t>& loser_txns);

    DiskManager* disk_mgr_;
    BufferPoolManager* bpm_;
    Catalog* catalog_;
    LogManager* log_mgr_;
    mutable std::mutex latch_;
};

} // namespace emberdb
