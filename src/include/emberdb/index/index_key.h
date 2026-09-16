#pragma once

#include "emberdb/common/types.h"
#include <cstring>
#include <string>

namespace emberdb {

constexpr size_t MAX_VARCHAR_KEY_LEN = 120;

/**
 * Trivially copyable, fixed-size 128-byte key representation for B+ Tree nodes.
 * Accommodates all EmberDB scalar types without dynamic heap allocation.
 */
struct IndexKey {
    TypeId type_id{TypeId::INVALID};
    bool is_null{false};
    uint16_t str_len{0};
    uint8_t padding[4]{0};
    union {
        bool bool_val;
        int32_t int_val;
        int64_t bigint_val;
        double double_val;
        char str_val[MAX_VARCHAR_KEY_LEN];
    };

    IndexKey();
    explicit IndexKey(const Value& val);
    explicit IndexKey(int32_t val);
    explicit IndexKey(int64_t val);
    explicit IndexKey(double val);
    explicit IndexKey(bool val);
    explicit IndexKey(const std::string& val);
    explicit IndexKey(const char* val);

    static IndexKey Null(TypeId type_id);

    Value ToValue() const;
    std::string ToString() const;

    bool operator==(const IndexKey& o) const;
    bool operator!=(const IndexKey& o) const { return !(*this == o); }
    bool operator<(const IndexKey& o) const;
    bool operator<=(const IndexKey& o) const { return *this < o || *this == o; }
    bool operator>(const IndexKey& o) const { return !(*this <= o); }
    bool operator>=(const IndexKey& o) const { return !(*this < o); }
};

static_assert(sizeof(IndexKey) == 128, "IndexKey must be exactly 128 bytes");

} // namespace emberdb
