#include "emberdb/recovery/recovery_manager.h"
#include "emberdb/index/index_key.h"
#include <algorithm>

namespace emberdb {

RecoveryManager::RecoveryManager(DiskManager* disk_mgr,
                                 BufferPoolManager* bpm,
                                 Catalog* catalog,
                                 LogManager* log_mgr)
    : disk_mgr_(disk_mgr), bpm_(bpm), catalog_(catalog), log_mgr_(log_mgr) {}

bool RecoveryManager::NeedsRecovery() {
    std::lock_guard<std::mutex> lock(latch_);
    if (!log_mgr_ || !log_mgr_->IsOpen()) {
        return false;
    }

    auto records = log_mgr_->ReadAllRecords();
    if (records.empty()) {
        return false;
    }

    std::unordered_set<txn_id_t> active_txns;
    std::unordered_set<txn_id_t> committed_txns;
    lsn_t max_lsn = INVALID_LSN;
    bool saw_clean_shutdown = false;

    AnalysisPass(records, active_txns, committed_txns, max_lsn, saw_clean_shutdown);

    return !saw_clean_shutdown || !active_txns.empty();
}

Result<RecoveryStats> RecoveryManager::Recover() {
    std::lock_guard<std::mutex> lock(latch_);
    RecoveryStats stats;

    if (!log_mgr_ || !log_mgr_->IsOpen()) {
        return Status::InvalidArgument("LogManager is not open");
    }

    auto records = log_mgr_->ReadAllRecords();
    stats.total_log_records = records.size();

    if (records.empty()) {
        stats.required = false;
        stats.shutdown_status = ShutdownStatus::CLEAN;
        return stats;
    }

    // 1. Analysis Pass
    std::unordered_set<txn_id_t> active_txns;
    std::unordered_set<txn_id_t> committed_txns;
    lsn_t max_lsn = INVALID_LSN;
    bool saw_clean_shutdown = false;

    AnalysisPass(records, active_txns, committed_txns, max_lsn, saw_clean_shutdown);

    stats.active_txns_count = active_txns.size();
    stats.committed_txns_count = committed_txns.size();
    stats.shutdown_status = (saw_clean_shutdown && active_txns.empty())
                                ? ShutdownStatus::CLEAN
                                : ShutdownStatus::UNCLEAN;
    stats.required = (stats.shutdown_status == ShutdownStatus::UNCLEAN);

    // 2. Redo Pass (Repeating history)
    stats.redone_records = RedoPass(records);

    // 3. Undo Pass (Rolling back loser transactions)
    if (!active_txns.empty()) {
        stats.undone_records = UndoPass(records, active_txns);
    }

    // 4. Checkpoint state after recovery
    if (bpm_) {
        bpm_->FlushAllPages();
    }

    LogRecord chk_rec(0, INVALID_LSN, LogRecordType::CHECKPOINT_END);
    lsn_t chk_lsn = log_mgr_->AppendRecord(chk_rec);
    log_mgr_->FlushLogBufferUpTo(chk_lsn);

    return stats;
}

Status RecoveryManager::RecordCleanShutdown() {
    std::lock_guard<std::mutex> lock(latch_);
    if (!log_mgr_ || !log_mgr_->IsOpen()) {
        return Status::InvalidArgument("LogManager is not open");
    }

    if (bpm_) {
        bpm_->FlushAllPages();
    }

    LogRecord chk_rec(0, INVALID_LSN, LogRecordType::CHECKPOINT_END);
    lsn_t lsn = log_mgr_->AppendRecord(chk_rec);
    log_mgr_->FlushLogBufferUpTo(lsn);

    return Status::OK();
}

void RecoveryManager::AnalysisPass(const std::vector<LogRecord>& records,
                                  std::unordered_set<txn_id_t>& active_txns,
                                  std::unordered_set<txn_id_t>& committed_txns,
                                  lsn_t& max_lsn,
                                  bool& saw_clean_shutdown) {
    active_txns.clear();
    committed_txns.clear();
    saw_clean_shutdown = false;
    max_lsn = INVALID_LSN;

    for (const auto& rec : records) {
        if (rec.GetLSN() > max_lsn) {
            max_lsn = rec.GetLSN();
        }

        switch (rec.GetType()) {
            case LogRecordType::CHECKPOINT_END:
                saw_clean_shutdown = true;
                active_txns.clear();
                break;
            case LogRecordType::BEGIN:
                active_txns.insert(rec.GetTxnId());
                saw_clean_shutdown = false;
                break;
            case LogRecordType::COMMIT:
                committed_txns.insert(rec.GetTxnId());
                active_txns.erase(rec.GetTxnId());
                break;
            case LogRecordType::ABORT:
                active_txns.erase(rec.GetTxnId());
                break;
            case LogRecordType::INSERT:
            case LogRecordType::UPDATE:
            case LogRecordType::DELETE:
                saw_clean_shutdown = false;
                if (rec.GetTxnId() > 0) {
                    active_txns.insert(rec.GetTxnId());
                }
                break;
            default:
                break;
        }
    }
}

size_t RecoveryManager::RedoPass(const std::vector<LogRecord>& records) {
    if (!bpm_ || !catalog_) {
        return 0;
    }

    size_t redone = 0;

    for (const auto& rec : records) {
        if (rec.GetType() != LogRecordType::INSERT &&
            rec.GetType() != LogRecordType::UPDATE &&
            rec.GetType() != LogRecordType::DELETE) {
            continue;
        }

        Table* table = catalog_->GetTable(rec.GetTableName());
        if (!table) {
            continue;
        }

        RID rid = rec.GetRID();
        if (!rid.IsValid()) {
            continue;
        }

        Page* page = bpm_->FetchPage(rid.page_id);
        if (!page) {
            continue;
        }

        if (page->GetLSN() < rec.GetLSN()) {
            auto indexes = catalog_->GetTableIndexes(rec.GetTableName());
            const Schema& schema = table->GetSchema();

            if (rec.GetType() == LogRecordType::INSERT) {
                SlottedPage sp(page->GetData());
                sp.RedoInsert(rid, rec.GetAfterImage());
                page->SetLSN(rec.GetLSN());
                bpm_->UnpinPage(rid.page_id, true);

                for (auto* idx_info : indexes) {
                    IndexKey key(rec.GetAfterImage().GetValue(schema, idx_info->GetColumnIdx()));
                    idx_info->GetIndex()->Insert(key, rid);
                }
                redone++;
            } else if (rec.GetType() == LogRecordType::UPDATE) {
                SlottedPage sp(page->GetData());
                sp.UpdateRecord(rid, rec.GetAfterImage());
                page->SetLSN(rec.GetLSN());
                bpm_->UnpinPage(rid.page_id, true);

                for (auto* idx_info : indexes) {
                    IndexKey old_key(rec.GetBeforeImage().GetValue(schema, idx_info->GetColumnIdx()));
                    IndexKey new_key(rec.GetAfterImage().GetValue(schema, idx_info->GetColumnIdx()));
                    if (!(old_key == new_key)) {
                        idx_info->GetIndex()->Remove(old_key, rid);
                        idx_info->GetIndex()->Insert(new_key, rid);
                    }
                }
                redone++;
            } else if (rec.GetType() == LogRecordType::DELETE) {
                SlottedPage sp(page->GetData());
                sp.DeleteRecord(rid);
                page->SetLSN(rec.GetLSN());
                bpm_->UnpinPage(rid.page_id, true);

                for (auto* idx_info : indexes) {
                    IndexKey key(rec.GetBeforeImage().GetValue(schema, idx_info->GetColumnIdx()));
                    idx_info->GetIndex()->Remove(key, rid);
                }
                redone++;
            }
        } else {
            bpm_->UnpinPage(rid.page_id, false);
        }
    }

    return redone;
}

size_t RecoveryManager::UndoPass(const std::vector<LogRecord>& records,
                                 const std::unordered_set<txn_id_t>& loser_txns) {
    if (!bpm_ || !catalog_ || loser_txns.empty()) {
        return 0;
    }

    size_t undone = 0;

    // Scan backwards from the end of the log
    for (auto it = records.rbegin(); it != records.rend(); ++it) {
        const auto& rec = *it;
        if (loser_txns.find(rec.GetTxnId()) == loser_txns.end()) {
            continue;
        }

        if (rec.GetType() != LogRecordType::INSERT &&
            rec.GetType() != LogRecordType::UPDATE &&
            rec.GetType() != LogRecordType::DELETE) {
            continue;
        }

        Table* table = catalog_->GetTable(rec.GetTableName());
        if (!table) {
            continue;
        }

        RID rid = rec.GetRID();
        if (!rid.IsValid()) {
            continue;
        }

        TableHeap* heap = table->GetTableHeap();
        auto indexes = catalog_->GetTableIndexes(rec.GetTableName());
        const Schema& schema = table->GetSchema();

        if (rec.GetType() == LogRecordType::INSERT) {
            // Undo insert: delete the inserted row
            heap->DeleteRecord(rid);
            for (auto* idx_info : indexes) {
                IndexKey key(rec.GetAfterImage().GetValue(schema, idx_info->GetColumnIdx()));
                idx_info->GetIndex()->Remove(key, rid);
            }
            undone++;
        } else if (rec.GetType() == LogRecordType::UPDATE) {
            // Undo update: restore before_image
            heap->UpdateRecord(rid, rec.GetBeforeImage());
            for (auto* idx_info : indexes) {
                IndexKey old_key(rec.GetBeforeImage().GetValue(schema, idx_info->GetColumnIdx()));
                IndexKey new_key(rec.GetAfterImage().GetValue(schema, idx_info->GetColumnIdx()));
                if (!(old_key == new_key)) {
                    idx_info->GetIndex()->Remove(new_key, rid);
                    idx_info->GetIndex()->Insert(old_key, rid);
                }
            }
            undone++;
        } else if (rec.GetType() == LogRecordType::DELETE) {
            // Undo delete: restore deleted row
            heap->RollbackDelete(rid, rec.GetBeforeImage());
            for (auto* idx_info : indexes) {
                IndexKey key(rec.GetBeforeImage().GetValue(schema, idx_info->GetColumnIdx()));
                idx_info->GetIndex()->Insert(key, rid);
            }
            undone++;
        }
    }

    // Write ABORT records for all loser transactions to log
    for (txn_id_t tid : loser_txns) {
        LogRecord abort_rec(tid, INVALID_LSN, LogRecordType::ABORT);
        log_mgr_->AppendRecord(abort_rec);
    }
    log_mgr_->FlushLogBuffer();

    return undone;
}

} // namespace emberdb
