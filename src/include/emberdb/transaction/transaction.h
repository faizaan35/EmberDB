#pragma once

#include "emberdb/common/types.h"
#include "emberdb/common/config.h"
#include "emberdb/storage/record/record.h"
#include <string>
#include <vector>

namespace emberdb {

enum class TransactionState {
    ACTIVE,
    COMMITTED,
    ABORTED
};

enum class TableWriteType {
    INSERT,
    UPDATE,
    DELETE
};

struct TableWriteRecord {
    TableWriteType write_type;
    std::string table_name;
    RID rid;
    Record before_image; // Valid for UPDATE and DELETE
    Record after_image;  // Valid for INSERT and UPDATE
};

class Transaction {
public:
    explicit Transaction(txn_id_t txn_id)
        : txn_id_(txn_id), state_(TransactionState::ACTIVE) {}

    txn_id_t GetTxnId() const { return txn_id_; }
    TransactionState GetState() const { return state_; }
    void SetState(TransactionState state) { state_ = state; }

    void AppendTableWrite(const TableWriteRecord& write) {
        table_writes_.push_back(write);
    }

    const std::vector<TableWriteRecord>& GetTableWrites() const {
        return table_writes_;
    }

    void ClearTableWrites() {
        table_writes_.clear();
    }

    lsn_t GetPrevLSN() const { return prev_lsn_; }
    void SetPrevLSN(lsn_t lsn) { prev_lsn_ = lsn; }

private:
    txn_id_t txn_id_{INVALID_TXN_ID};
    TransactionState state_{TransactionState::ACTIVE};
    lsn_t prev_lsn_{INVALID_LSN};
    std::vector<TableWriteRecord> table_writes_;
};

} // namespace emberdb
