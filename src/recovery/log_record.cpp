#include "emberdb/recovery/log_record.h"
#include <cstring>
#include <sstream>

namespace emberdb {

static constexpr size_t LOG_HEADER_SIZE = 32;

LogRecord::LogRecord(txn_id_t txn_id, lsn_t prev_lsn, LogRecordType type)
    : prev_lsn_(prev_lsn), txn_id_(txn_id), type_(type) {
    size_ = CalculateSize();
}

LogRecord::LogRecord(txn_id_t txn_id, lsn_t prev_lsn, LogRecordType type,
                     std::string table_name, RID rid, Record before_image, Record after_image)
    : prev_lsn_(prev_lsn), txn_id_(txn_id), type_(type),
      table_name_(std::move(table_name)), rid_(rid),
      before_image_(std::move(before_image)), after_image_(std::move(after_image)) {
    size_ = CalculateSize();
}

LogRecord LogRecord::CreateBegin(txn_id_t txn_id) {
    return LogRecord(txn_id, INVALID_LSN, LogRecordType::BEGIN);
}

LogRecord LogRecord::CreateCommit(txn_id_t txn_id, lsn_t prev_lsn) {
    return LogRecord(txn_id, prev_lsn, LogRecordType::COMMIT);
}

LogRecord LogRecord::CreateAbort(txn_id_t txn_id, lsn_t prev_lsn) {
    return LogRecord(txn_id, prev_lsn, LogRecordType::ABORT);
}

LogRecord LogRecord::CreateInsert(txn_id_t txn_id, lsn_t prev_lsn,
                                  std::string table_name, RID rid, Record after_image) {
    return LogRecord(txn_id, prev_lsn, LogRecordType::INSERT,
                     std::move(table_name), rid, Record(), std::move(after_image));
}

LogRecord LogRecord::CreateUpdate(txn_id_t txn_id, lsn_t prev_lsn,
                                  std::string table_name, RID rid, Record before_image, Record after_image) {
    return LogRecord(txn_id, prev_lsn, LogRecordType::UPDATE,
                     std::move(table_name), rid, std::move(before_image), std::move(after_image));
}

LogRecord LogRecord::CreateDelete(txn_id_t txn_id, lsn_t prev_lsn,
                                  std::string table_name, RID rid, Record before_image) {
    return LogRecord(txn_id, prev_lsn, LogRecordType::DELETE,
                     std::move(table_name), rid, std::move(before_image), Record());
}

uint32_t LogRecord::CalculateSize() const {
    uint32_t total = LOG_HEADER_SIZE;
    if (type_ == LogRecordType::INSERT) {
        total += sizeof(uint32_t) + static_cast<uint32_t>(table_name_.size());
        total += sizeof(page_id_t) + sizeof(slot_id_t);
        total += sizeof(uint32_t) + after_image_.GetLength();
    } else if (type_ == LogRecordType::UPDATE) {
        total += sizeof(uint32_t) + static_cast<uint32_t>(table_name_.size());
        total += sizeof(page_id_t) + sizeof(slot_id_t);
        total += sizeof(uint32_t) + before_image_.GetLength();
        total += sizeof(uint32_t) + after_image_.GetLength();
    } else if (type_ == LogRecordType::DELETE) {
        total += sizeof(uint32_t) + static_cast<uint32_t>(table_name_.size());
        total += sizeof(page_id_t) + sizeof(slot_id_t);
        total += sizeof(uint32_t) + before_image_.GetLength();
    }
    return total;
}

void LogRecord::Serialize(char* dest) const {
    size_t offset = 0;

    // Header
    uint32_t rec_size = (size_ > 0) ? size_ : CalculateSize();
    std::memcpy(dest + offset, &rec_size, sizeof(uint32_t));
    offset += sizeof(uint32_t);

    std::memcpy(dest + offset, &lsn_, sizeof(lsn_t));
    offset += sizeof(lsn_t);

    std::memcpy(dest + offset, &prev_lsn_, sizeof(lsn_t));
    offset += sizeof(lsn_t);

    std::memcpy(dest + offset, &txn_id_, sizeof(txn_id_t));
    offset += sizeof(txn_id_t);

    uint32_t type_val = static_cast<uint32_t>(type_);
    std::memcpy(dest + offset, &type_val, sizeof(uint32_t));
    offset += sizeof(uint32_t);

    // Payload for DML
    if (type_ == LogRecordType::INSERT || type_ == LogRecordType::UPDATE || type_ == LogRecordType::DELETE) {
        uint32_t name_len = static_cast<uint32_t>(table_name_.size());
        std::memcpy(dest + offset, &name_len, sizeof(uint32_t));
        offset += sizeof(uint32_t);

        if (name_len > 0) {
            std::memcpy(dest + offset, table_name_.data(), name_len);
            offset += name_len;
        }

        page_id_t pid = rid_.page_id;
        slot_id_t sid = rid_.slot_id;
        std::memcpy(dest + offset, &pid, sizeof(page_id_t));
        offset += sizeof(page_id_t);
        std::memcpy(dest + offset, &sid, sizeof(slot_id_t));
        offset += sizeof(slot_id_t);

        if (type_ == LogRecordType::INSERT) {
            uint32_t after_len = after_image_.GetLength();
            std::memcpy(dest + offset, &after_len, sizeof(uint32_t));
            offset += sizeof(uint32_t);
            if (after_len > 0) {
                std::memcpy(dest + offset, after_image_.GetData(), after_len);
                offset += after_len;
            }
        } else if (type_ == LogRecordType::UPDATE) {
            uint32_t before_len = before_image_.GetLength();
            std::memcpy(dest + offset, &before_len, sizeof(uint32_t));
            offset += sizeof(uint32_t);
            if (before_len > 0) {
                std::memcpy(dest + offset, before_image_.GetData(), before_len);
                offset += before_len;
            }

            uint32_t after_len = after_image_.GetLength();
            std::memcpy(dest + offset, &after_len, sizeof(uint32_t));
            offset += sizeof(uint32_t);
            if (after_len > 0) {
                std::memcpy(dest + offset, after_image_.GetData(), after_len);
                offset += after_len;
            }
        } else if (type_ == LogRecordType::DELETE) {
            uint32_t before_len = before_image_.GetLength();
            std::memcpy(dest + offset, &before_len, sizeof(uint32_t));
            offset += sizeof(uint32_t);
            if (before_len > 0) {
                std::memcpy(dest + offset, before_image_.GetData(), before_len);
                offset += before_len;
            }
        }
    }
}

bool LogRecord::Deserialize(const char* src, size_t max_bytes, LogRecord& out) {
    if (max_bytes < LOG_HEADER_SIZE) {
        return false;
    }

    size_t offset = 0;
    uint32_t rec_size = 0;
    std::memcpy(&rec_size, src + offset, sizeof(uint32_t));
    offset += sizeof(uint32_t);

    if (rec_size > max_bytes || rec_size < LOG_HEADER_SIZE) {
        return false;
    }

    out.size_ = rec_size;

    std::memcpy(&out.lsn_, src + offset, sizeof(lsn_t));
    offset += sizeof(lsn_t);

    std::memcpy(&out.prev_lsn_, src + offset, sizeof(lsn_t));
    offset += sizeof(lsn_t);

    std::memcpy(&out.txn_id_, src + offset, sizeof(txn_id_t));
    offset += sizeof(txn_id_t);

    uint32_t type_val = 0;
    std::memcpy(&type_val, src + offset, sizeof(uint32_t));
    offset += sizeof(uint32_t);
    out.type_ = static_cast<LogRecordType>(type_val);

    out.table_name_.clear();
    out.rid_ = RID{};
    out.before_image_ = Record();
    out.after_image_ = Record();

    if (out.type_ == LogRecordType::INSERT || out.type_ == LogRecordType::UPDATE || out.type_ == LogRecordType::DELETE) {
        if (offset + sizeof(uint32_t) > rec_size) return false;
        uint32_t name_len = 0;
        std::memcpy(&name_len, src + offset, sizeof(uint32_t));
        offset += sizeof(uint32_t);

        if (offset + name_len + sizeof(page_id_t) + sizeof(slot_id_t) > rec_size) return false;
        if (name_len > 0) {
            out.table_name_.assign(src + offset, name_len);
            offset += name_len;
        }

        page_id_t pid = INVALID_PAGE_ID;
        slot_id_t sid = INVALID_SLOT_ID;
        std::memcpy(&pid, src + offset, sizeof(page_id_t));
        offset += sizeof(page_id_t);
        std::memcpy(&sid, src + offset, sizeof(slot_id_t));
        offset += sizeof(slot_id_t);
        out.rid_ = RID{pid, sid};

        if (out.type_ == LogRecordType::INSERT) {
            if (offset + sizeof(uint32_t) > rec_size) return false;
            uint32_t after_len = 0;
            std::memcpy(&after_len, src + offset, sizeof(uint32_t));
            offset += sizeof(uint32_t);

            if (offset + after_len > rec_size) return false;
            if (after_len > 0) {
                out.after_image_ = Record(src + offset, after_len, out.rid_);
                offset += after_len;
            }
        } else if (out.type_ == LogRecordType::UPDATE) {
            if (offset + sizeof(uint32_t) > rec_size) return false;
            uint32_t before_len = 0;
            std::memcpy(&before_len, src + offset, sizeof(uint32_t));
            offset += sizeof(uint32_t);

            if (offset + before_len > rec_size) return false;
            if (before_len > 0) {
                out.before_image_ = Record(src + offset, before_len, out.rid_);
                offset += before_len;
            }

            if (offset + sizeof(uint32_t) > rec_size) return false;
            uint32_t after_len = 0;
            std::memcpy(&after_len, src + offset, sizeof(uint32_t));
            offset += sizeof(uint32_t);

            if (offset + after_len > rec_size) return false;
            if (after_len > 0) {
                out.after_image_ = Record(src + offset, after_len, out.rid_);
                offset += after_len;
            }
        } else if (out.type_ == LogRecordType::DELETE) {
            if (offset + sizeof(uint32_t) > rec_size) return false;
            uint32_t before_len = 0;
            std::memcpy(&before_len, src + offset, sizeof(uint32_t));
            offset += sizeof(uint32_t);

            if (offset + before_len > rec_size) return false;
            if (before_len > 0) {
                out.before_image_ = Record(src + offset, before_len, out.rid_);
                offset += before_len;
            }
        }
    }

    return true;
}

std::string LogRecord::ToString() const {
    std::ostringstream oss;
    oss << "LogRecord(lsn=" << lsn_ << ", prev_lsn=" << prev_lsn_
        << ", txn_id=" << txn_id_ << ", type=";
    switch (type_) {
        case LogRecordType::BEGIN: oss << "BEGIN"; break;
        case LogRecordType::COMMIT: oss << "COMMIT"; break;
        case LogRecordType::ABORT: oss << "ABORT"; break;
        case LogRecordType::INSERT: oss << "INSERT, table=" << table_name_ << ", rid=" << rid_.ToString(); break;
        case LogRecordType::UPDATE: oss << "UPDATE, table=" << table_name_ << ", rid=" << rid_.ToString(); break;
        case LogRecordType::DELETE: oss << "DELETE, table=" << table_name_ << ", rid=" << rid_.ToString(); break;
        case LogRecordType::CHECKPOINT_BEGIN: oss << "CHECKPOINT_BEGIN"; break;
        case LogRecordType::CHECKPOINT_END: oss << "CHECKPOINT_END"; break;
        default: oss << "UNKNOWN"; break;
    }
    oss << ", size=" << size_ << ")";
    return oss.str();
}

} // namespace emberdb
