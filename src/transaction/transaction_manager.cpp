#include "emberdb/transaction/transaction_manager.h"
#include "emberdb/recovery/log_manager.h"

namespace emberdb {

std::shared_ptr<Transaction> TransactionManager::Begin() {
    std::lock_guard<std::mutex> lock(latch_);
    txn_id_t tid = next_txn_id_++;
    auto txn = std::make_shared<Transaction>(tid);
    txn_map_[tid] = txn;

    if (log_mgr_) {
        LogRecord begin_rec(tid, INVALID_LSN, LogRecordType::BEGIN);
        lsn_t lsn = log_mgr_->AppendRecord(begin_rec);
        txn->SetPrevLSN(lsn);
    }

    return txn;
}

Status TransactionManager::Commit(Transaction* txn) {
    if (!txn) {
        return Status::InvalidArgument("Null transaction pointer");
    }

    std::lock_guard<std::mutex> lock(latch_);
    if (txn->GetState() != TransactionState::ACTIVE) {
        return Status::TransactionError("Transaction is not ACTIVE");
    }

    if (log_mgr_) {
        LogRecord commit_rec(txn->GetTxnId(), txn->GetPrevLSN(), LogRecordType::COMMIT);
        lsn_t lsn = log_mgr_->AppendRecord(commit_rec);
        txn->SetPrevLSN(lsn);
        log_mgr_->FlushLogBufferUpTo(lsn);
    }

    txn->SetState(TransactionState::COMMITTED);
    return Status::OK();
}

Status TransactionManager::Abort(Transaction* txn, Catalog* catalog) {
    if (!txn) {
        return Status::InvalidArgument("Null transaction pointer");
    }
    if (!catalog) {
        return Status::InvalidArgument("Null catalog pointer");
    }

    std::lock_guard<std::mutex> lock(latch_);
    if (txn->GetState() != TransactionState::ACTIVE) {
        return Status::TransactionError("Transaction is not ACTIVE");
    }

    // Rollback operations in reverse order
    const auto& writes = txn->GetTableWrites();
    for (auto it = writes.rbegin(); it != writes.rend(); ++it) {
        const auto& write = *it;
        Table* table = catalog->GetTable(write.table_name);
        if (!table) continue;

        TableHeap* heap = table->GetTableHeap();
        auto indexes = catalog->GetTableIndexes(write.table_name);

        if (write.write_type == TableWriteType::INSERT) {
            // Delete inserted row from table heap
            heap->DeleteRecord(write.rid);

            // Remove corresponding key from all indexes on this table
            for (auto* idx_info : indexes) {
                IndexKey key(write.after_image.GetValue(table->GetSchema(), idx_info->GetColumnIdx()));
                idx_info->GetIndex()->Remove(key, write.rid);
            }
        } else if (write.write_type == TableWriteType::UPDATE) {
            // Restore before_image in table heap
            heap->UpdateRecord(write.rid, write.before_image);

            // Synchronize indexes if indexed column changed
            for (auto* idx_info : indexes) {
                IndexKey old_key(write.before_image.GetValue(table->GetSchema(), idx_info->GetColumnIdx()));
                IndexKey new_key(write.after_image.GetValue(table->GetSchema(), idx_info->GetColumnIdx()));
                if (!(old_key == new_key)) {
                    idx_info->GetIndex()->Remove(new_key, write.rid);
                    idx_info->GetIndex()->Insert(old_key, write.rid);
                }
            }
        } else if (write.write_type == TableWriteType::DELETE) {
            // Restore deleted row in table heap
            heap->RollbackDelete(write.rid, write.before_image);

            // Re-insert into all indexes on this table
            for (auto* idx_info : indexes) {
                IndexKey key(write.before_image.GetValue(table->GetSchema(), idx_info->GetColumnIdx()));
                idx_info->GetIndex()->Insert(key, write.rid);
            }
        }
    }

    if (log_mgr_) {
        LogRecord abort_rec(txn->GetTxnId(), txn->GetPrevLSN(), LogRecordType::ABORT);
        lsn_t lsn = log_mgr_->AppendRecord(abort_rec);
        txn->SetPrevLSN(lsn);
        log_mgr_->FlushLogBufferUpTo(lsn);
    }

    txn->SetState(TransactionState::ABORTED);
    return Status::OK();
}

std::shared_ptr<Transaction> TransactionManager::GetTransaction(txn_id_t txn_id) {
    std::lock_guard<std::mutex> lock(latch_);
    auto it = txn_map_.find(txn_id);
    if (it != txn_map_.end()) {
        return it->second;
    }
    return nullptr;
}

} // namespace emberdb
