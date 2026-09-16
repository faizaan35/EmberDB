#pragma once

#include "emberdb/transaction/transaction.h"
#include "emberdb/catalog/catalog.h"
#include "emberdb/common/status.h"
#include <mutex>
#include <memory>
#include <unordered_map>

namespace emberdb {

class TransactionManager {
public:
    explicit TransactionManager(class LogManager* log_mgr = nullptr) : log_mgr_(log_mgr) {}
    ~TransactionManager() = default;

    void SetLogManager(class LogManager* log_mgr) { log_mgr_ = log_mgr; }
    class LogManager* GetLogManager() const { return log_mgr_; }

    std::shared_ptr<Transaction> Begin();
    Status Commit(Transaction* txn);
    Status Abort(Transaction* txn, Catalog* catalog);

    std::shared_ptr<Transaction> GetTransaction(txn_id_t txn_id);

private:
    std::mutex latch_;
    class LogManager* log_mgr_{nullptr};
    txn_id_t next_txn_id_{1};
    std::unordered_map<txn_id_t, std::shared_ptr<Transaction>> txn_map_;
};

} // namespace emberdb
