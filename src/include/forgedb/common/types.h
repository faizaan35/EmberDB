#pragma once

#include "forgedb/common/config.h"
#include <string>
#include <variant>
#include <vector>
#include <cstring>
#include <sstream>
#include <stdexcept>

namespace forgedb {

enum class TypeId : uint8_t {
    INVALID = 0,
    BOOLEAN,
    INTEGER,
    BIGINT,
    DOUBLE,
    VARCHAR
};

std::string TypeIdToString(TypeId type_id);

/**
 * Record Identifier (RID) represents the physical location of a record in a table.
 * Consists of a page_id and a slot_id within that page.
 */
struct RID {
    page_id_t page_id{INVALID_PAGE_ID};
    slot_id_t slot_id{INVALID_SLOT_ID};

    RID() = default;
    RID(page_id_t p_id, slot_id_t s_id) : page_id(p_id), slot_id(s_id) {}

    bool IsValid() const {
        return page_id != INVALID_PAGE_ID && slot_id != INVALID_SLOT_ID;
    }

    std::string ToString() const {
        return "RID(" + std::to_string(page_id) + ", " + std::to_string(slot_id) + ")";
    }

    bool operator==(const RID& o) const {
        return page_id == o.page_id && slot_id == o.slot_id;
    }

    bool operator!=(const RID& o) const {
        return !(*this == o);
    }

    bool operator<(const RID& o) const {
        if (page_id != o.page_id) return page_id < o.page_id;
        return slot_id < o.slot_id;
    }
};

/**
 * Tagged Value class representing a typed scalar datum in ForgeDB.
 */
class Value {
public:
    Value() : type_id_(TypeId::INVALID), is_null_(true), val_(std::monostate{}) {}
    explicit Value(TypeId type_id) : type_id_(type_id), is_null_(true), val_(std::monostate{}) {}
    Value(bool val) : type_id_(TypeId::BOOLEAN), is_null_(false), val_(val) {}
    Value(int32_t val) : type_id_(TypeId::INTEGER), is_null_(false), val_(val) {}
    Value(int64_t val) : type_id_(TypeId::BIGINT), is_null_(false), val_(val) {}
    Value(double val) : type_id_(TypeId::DOUBLE), is_null_(false), val_(val) {}
    Value(std::string val) : type_id_(TypeId::VARCHAR), is_null_(false), val_(std::move(val)) {}
    Value(const char* val) : type_id_(TypeId::VARCHAR), is_null_(false), val_(std::string(val)) {}

    static Value Null(TypeId type_id) {
        return Value(type_id);
    }

    bool IsNull() const { return is_null_; }
    TypeId GetTypeId() const { return type_id_; }

    bool GetAsBoolean() const {
        CheckType(TypeId::BOOLEAN);
        return std::get<bool>(val_);
    }

    int32_t GetAsInteger() const {
        CheckType(TypeId::INTEGER);
        return std::get<int32_t>(val_);
    }

    int64_t GetAsBigInt() const {
        if (type_id_ == TypeId::INTEGER) return static_cast<int64_t>(std::get<int32_t>(val_));
        CheckType(TypeId::BIGINT);
        return std::get<int64_t>(val_);
    }

    double GetAsDouble() const {
        if (type_id_ == TypeId::INTEGER) return static_cast<double>(std::get<int32_t>(val_));
        if (type_id_ == TypeId::BIGINT) return static_cast<double>(std::get<int64_t>(val_));
        CheckType(TypeId::DOUBLE);
        return std::get<double>(val_);
    }

    const std::string& GetAsVarChar() const {
        CheckType(TypeId::VARCHAR);
        return std::get<std::string>(val_);
    }

    std::string ToString() const;

    // Serialization
    size_t GetSerializedSize() const;
    void SerializeTo(char* dest) const;
    static Value DeserializeFrom(const char* src, TypeId type_id, size_t& bytes_read);

    // Comparisons
    bool operator==(const Value& o) const;
    bool operator!=(const Value& o) const { return !(*this == o); }
    bool operator<(const Value& o) const;
    bool operator<=(const Value& o) const { return *this < o || *this == o; }
    bool operator>(const Value& o) const { return !(*this <= o); }
    bool operator>=(const Value& o) const { return !(*this < o); }

private:
    void CheckType(TypeId expected) const {
        if (is_null_) throw std::runtime_error("Attempted to access value of NULL Value");
        if (type_id_ != expected) {
            throw std::runtime_error("Type mismatch: expected " + TypeIdToString(expected) + 
                                     ", got " + TypeIdToString(type_id_));
        }
    }

    TypeId type_id_;
    bool is_null_;
    std::variant<std::monostate, bool, int32_t, int64_t, double, std::string> val_;
};

} // namespace forgedb
