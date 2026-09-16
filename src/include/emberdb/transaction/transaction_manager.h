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
    TransactionManager() = default;
    ~TransactionManager() = default;

    std::shared_ptr<Transaction> Begin();
    Status Commit(Transaction* txn);
    Status Abort(Transaction* txn, Catalog* catalog);

    std::shared_ptr<Transaction> GetTransaction(txn_id_t txn_id);

private:
    std::mutex latch_;
    txn_id_t next_txn_id_{1};
    std::unordered_map<txn_id_t, std::shared_ptr<Transaction>> txn_map_;
};

} // namespace emberdb
