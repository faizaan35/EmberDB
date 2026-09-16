#pragma once

#include "emberdb/common/config.h"
#include "emberdb/common/types.h"
#include "emberdb/storage/record/record.h"
#include <cstdint>
#include <string>
#include <vector>

namespace emberdb {

/**
 * LogRecordType distinguishes transaction lifecycle markers and data mutations.
 */
enum class LogRecordType : uint32_t {
    INVALID = 0,
    BEGIN = 1,
    COMMIT = 2,
    ABORT = 3,
    INSERT = 4,
    UPDATE = 5,
    DELETE = 6,
    CHECKPOINT_BEGIN = 7,
    CHECKPOINT_END = 8
};

/**
 * LogRecord encapsulates a single write-ahead log entry.
 * Supports binary serialization and deserialization.
 */
class LogRecord {
public:
    LogRecord() = default;

    // Transaction lifecycle records (BEGIN, COMMIT, ABORT)
    LogRecord(txn_id_t txn_id, lsn_t prev_lsn, LogRecordType type);

    // Full DML record constructor
    LogRecord(txn_id_t txn_id, lsn_t prev_lsn, LogRecordType type,
              std::string table_name, RID rid, Record before_image, Record after_image);

    // Factory methods
    static LogRecord CreateBegin(txn_id_t txn_id);
    static LogRecord CreateCommit(txn_id_t txn_id, lsn_t prev_lsn);
    static LogRecord CreateAbort(txn_id_t txn_id, lsn_t prev_lsn);
    static LogRecord CreateInsert(txn_id_t txn_id, lsn_t prev_lsn,
                                  std::string table_name, RID rid, Record after_image);
    static LogRecord CreateUpdate(txn_id_t txn_id, lsn_t prev_lsn,
                                  std::string table_name, RID rid, Record before_image, Record after_image);
    static LogRecord CreateDelete(txn_id_t txn_id, lsn_t prev_lsn,
                                  std::string table_name, RID rid, Record before_image);

    // Getters
    uint32_t GetSize() const { return size_; }
    lsn_t GetLSN() const { return lsn_; }
    void SetLSN(lsn_t lsn) { lsn_ = lsn; }

    lsn_t GetPrevLSN() const { return prev_lsn_; }
    txn_id_t GetTxnId() const { return txn_id_; }
    LogRecordType GetType() const { return type_; }

    const std::string& GetTableName() const { return table_name_; }
    RID GetRID() const { return rid_; }
    const Record& GetBeforeImage() const { return before_image_; }
    const Record& GetAfterImage() const { return after_image_; }

    // Binary serialization
    uint32_t CalculateSize() const;
    void Serialize(char* dest) const;
    static bool Deserialize(const char* src, size_t max_bytes, LogRecord& out_record);

    std::string ToString() const;

private:
    uint32_t size_{0};
    lsn_t lsn_{INVALID_LSN};
    lsn_t prev_lsn_{INVALID_LSN};
    txn_id_t txn_id_{INVALID_TXN_ID};
    LogRecordType type_{LogRecordType::INVALID};

    // DML payload
    std::string table_name_;
    RID rid_{};
    Record before_image_;
    Record after_image_;
};

} // namespace emberdb
