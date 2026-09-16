#include "emberdb/index/index_key.h"
#include <algorithm>
#include <stdexcept>

namespace emberdb {

IndexKey::IndexKey() : type_id(TypeId::INVALID), is_null(true), str_len(0) {
    std::memset(padding, 0, sizeof(padding));
    std::memset(str_val, 0, sizeof(str_val));
}

IndexKey::IndexKey(const Value& val) : type_id(val.GetTypeId()), is_null(val.IsNull()), str_len(0) {
    std::memset(padding, 0, sizeof(padding));
    std::memset(str_val, 0, sizeof(str_val));

    if (is_null) return;

    switch (type_id) {
        case TypeId::BOOLEAN:
            bool_val = val.GetAsBoolean();
            break;
        case TypeId::INTEGER:
            int_val = val.GetAsInteger();
            break;
        case TypeId::BIGINT:
            bigint_val = val.GetAsBigInt();
            break;
        case TypeId::DOUBLE:
            double_val = val.GetAsDouble();
            break;
        case TypeId::VARCHAR: {
            const auto& s = val.GetAsVarChar();
            size_t copy_len = std::min(s.size(), MAX_VARCHAR_KEY_LEN);
            str_len = static_cast<uint16_t>(copy_len);
            std::memcpy(str_val, s.data(), copy_len);
            break;
        }
        default:
            break;
    }
}

IndexKey::IndexKey(int32_t val) : type_id(TypeId::INTEGER), is_null(false), str_len(0), int_val(val) {
    std::memset(padding, 0, sizeof(padding));
}

IndexKey::IndexKey(int64_t val) : type_id(TypeId::BIGINT), is_null(false), str_len(0), bigint_val(val) {
    std::memset(padding, 0, sizeof(padding));
}

IndexKey::IndexKey(double val) : type_id(TypeId::DOUBLE), is_null(false), str_len(0), double_val(val) {
    std::memset(padding, 0, sizeof(padding));
}

IndexKey::IndexKey(bool val) : type_id(TypeId::BOOLEAN), is_null(false), str_len(0), bool_val(val) {
    std::memset(padding, 0, sizeof(padding));
}

IndexKey::IndexKey(const std::string& val) : type_id(TypeId::VARCHAR), is_null(false) {
    std::memset(padding, 0, sizeof(padding));
    std::memset(str_val, 0, sizeof(str_val));
    size_t copy_len = std::min(val.size(), MAX_VARCHAR_KEY_LEN);
    str_len = static_cast<uint16_t>(copy_len);
    std::memcpy(str_val, val.data(), copy_len);
}

IndexKey::IndexKey(const char* val) : IndexKey(std::string(val)) {}

IndexKey IndexKey::Null(TypeId type_id) {
    IndexKey k;
    k.type_id = type_id;
    k.is_null = true;
    return k;
}

Value IndexKey::ToValue() const {
    if (is_null) {
        return Value::Null(type_id);
    }
    switch (type_id) {
        case TypeId::BOOLEAN:
            return Value(bool_val);
        case TypeId::INTEGER:
            return Value(int_val);
        case TypeId::BIGINT:
            return Value(bigint_val);
        case TypeId::DOUBLE:
            return Value(double_val);
        case TypeId::VARCHAR:
            return Value(std::string(str_val, str_len));
        default:
            return Value::Null(type_id);
    }
}

std::string IndexKey::ToString() const {
    return ToValue().ToString();
}

bool IndexKey::operator==(const IndexKey& o) const {
    if (is_null && o.is_null) return true;
    if (is_null != o.is_null) return false;
    if (type_id != o.type_id) {
        // Cross-numeric comparison
        if ((type_id == TypeId::INTEGER || type_id == TypeId::BIGINT) &&
            (o.type_id == TypeId::INTEGER || o.type_id == TypeId::BIGINT)) {
            int64_t a = (type_id == TypeId::INTEGER) ? int_val : bigint_val;
            int64_t b = (o.type_id == TypeId::INTEGER) ? o.int_val : o.bigint_val;
            return a == b;
        }
        return false;
    }

    switch (type_id) {
        case TypeId::BOOLEAN:
            return bool_val == o.bool_val;
        case TypeId::INTEGER:
            return int_val == o.int_val;
        case TypeId::BIGINT:
            return bigint_val == o.bigint_val;
        case TypeId::DOUBLE:
            return double_val == o.double_val;
        case TypeId::VARCHAR:
            if (str_len != o.str_len) return false;
            return std::memcmp(str_val, o.str_val, str_len) == 0;
        default:
            return true;
    }
}

bool IndexKey::operator<(const IndexKey& o) const {
    // NULLs sort first
    if (is_null && !o.is_null) return true;
    if (!is_null && o.is_null) return false;
    if (is_null && o.is_null) return false;

    if (type_id != o.type_id) {
        if ((type_id == TypeId::INTEGER || type_id == TypeId::BIGINT) &&
            (o.type_id == TypeId::INTEGER || o.type_id == TypeId::BIGINT)) {
            int64_t a = (type_id == TypeId::INTEGER) ? int_val : bigint_val;
            int64_t b = (o.type_id == TypeId::INTEGER) ? o.int_val : o.bigint_val;
            return a < b;
        }
        if ((type_id == TypeId::INTEGER || type_id == TypeId::BIGINT || type_id == TypeId::DOUBLE) &&
            (o.type_id == TypeId::INTEGER || o.type_id == TypeId::BIGINT || o.type_id == TypeId::DOUBLE)) {
            double a = (type_id == TypeId::INTEGER) ? int_val : ((type_id == TypeId::BIGINT) ? static_cast<double>(bigint_val) : double_val);
            double b = (o.type_id == TypeId::INTEGER) ? o.int_val : ((o.type_id == TypeId::BIGINT) ? static_cast<double>(o.bigint_val) : o.double_val);
            return a < b;
        }
        return type_id < o.type_id;
    }

    switch (type_id) {
        case TypeId::BOOLEAN:
            return !bool_val && o.bool_val;
        case TypeId::INTEGER:
            return int_val < o.int_val;
        case TypeId::BIGINT:
            return bigint_val < o.bigint_val;
        case TypeId::DOUBLE:
            return double_val < o.double_val;
        case TypeId::VARCHAR: {
            size_t min_len = std::min<size_t>(str_len, o.str_len);
            int cmp = std::memcmp(str_val, o.str_val, min_len);
            if (cmp != 0) return cmp < 0;
            return str_len < o.str_len;
        }
        default:
            return false;
    }
}

} // namespace emberdb
