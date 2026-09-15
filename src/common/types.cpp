#include "emberdb/common/types.h"

namespace emberdb {

std::string TypeIdToString(TypeId type_id) {
    switch (type_id) {
        case TypeId::INVALID: return "INVALID";
        case TypeId::BOOLEAN: return "BOOLEAN";
        case TypeId::INTEGER: return "INT";
        case TypeId::BIGINT:  return "BIGINT";
        case TypeId::DOUBLE:  return "DOUBLE";
        case TypeId::VARCHAR: return "VARCHAR";
    }
    return "UNKNOWN";
}

std::string Value::ToString() const {
    if (is_null_) {
        return "NULL";
    }
    switch (type_id_) {
        case TypeId::BOOLEAN:
            return std::get<bool>(val_) ? "true" : "false";
        case TypeId::INTEGER:
            return std::to_string(std::get<int32_t>(val_));
        case TypeId::BIGINT:
            return std::to_string(std::get<int64_t>(val_));
        case TypeId::DOUBLE: {
            std::ostringstream ss;
            ss << std::get<double>(val_);
            return ss.str();
        }
        case TypeId::VARCHAR:
            return std::get<std::string>(val_);
        default:
            return "UNKNOWN";
    }
}

size_t Value::GetSerializedSize() const {
    // 1 byte is_null indicator
    if (is_null_) {
        return 1;
    }
    switch (type_id_) {
        case TypeId::BOOLEAN:
            return 1 + sizeof(uint8_t);
        case TypeId::INTEGER:
            return 1 + sizeof(int32_t);
        case TypeId::BIGINT:
            return 1 + sizeof(int64_t);
        case TypeId::DOUBLE:
            return 1 + sizeof(double);
        case TypeId::VARCHAR: {
            const auto& str = std::get<std::string>(val_);
            return 1 + sizeof(uint16_t) + str.size();
        }
        default:
            return 1;
    }
}

void Value::SerializeTo(char* dest) const {
    uint8_t null_byte = is_null_ ? 1 : 0;
    std::memcpy(dest, &null_byte, 1);
    dest += 1;

    if (is_null_) return;

    switch (type_id_) {
        case TypeId::BOOLEAN: {
            uint8_t b = std::get<bool>(val_) ? 1 : 0;
            std::memcpy(dest, &b, sizeof(b));
            break;
        }
        case TypeId::INTEGER: {
            int32_t i = std::get<int32_t>(val_);
            std::memcpy(dest, &i, sizeof(i));
            break;
        }
        case TypeId::BIGINT: {
            int64_t bi = std::get<int64_t>(val_);
            std::memcpy(dest, &bi, sizeof(bi));
            break;
        }
        case TypeId::DOUBLE: {
            double d = std::get<double>(val_);
            std::memcpy(dest, &d, sizeof(d));
            break;
        }
        case TypeId::VARCHAR: {
            const auto& str = std::get<std::string>(val_);
            uint16_t len = static_cast<uint16_t>(str.size());
            std::memcpy(dest, &len, sizeof(len));
            dest += sizeof(len);
            std::memcpy(dest, str.data(), len);
            break;
        }
        default:
            break;
    }
}

Value Value::DeserializeFrom(const char* src, TypeId type_id, size_t& bytes_read) {
    uint8_t null_byte = 0;
    std::memcpy(&null_byte, src, 1);
    src += 1;
    bytes_read = 1;

    if (null_byte != 0) {
        return Value::Null(type_id);
    }

    switch (type_id) {
        case TypeId::BOOLEAN: {
            uint8_t b = 0;
            std::memcpy(&b, src, sizeof(b));
            bytes_read += sizeof(b);
            return Value(b != 0);
        }
        case TypeId::INTEGER: {
            int32_t i = 0;
            std::memcpy(&i, src, sizeof(i));
            bytes_read += sizeof(i);
            return Value(i);
        }
        case TypeId::BIGINT: {
            int64_t bi = 0;
            std::memcpy(&bi, src, sizeof(bi));
            bytes_read += sizeof(bi);
            return Value(bi);
        }
        case TypeId::DOUBLE: {
            double d = 0.0;
            std::memcpy(&d, src, sizeof(d));
            bytes_read += sizeof(d);
            return Value(d);
        }
        case TypeId::VARCHAR: {
            uint16_t len = 0;
            std::memcpy(&len, src, sizeof(len));
            src += sizeof(len);
            bytes_read += sizeof(len);
            std::string str(src, len);
            bytes_read += len;
            return Value(std::move(str));
        }
        default:
            throw std::runtime_error("Cannot deserialize INVALID type");
    }
}

bool Value::operator==(const Value& o) const {
    if (is_null_ && o.is_null_) return true;
    if (is_null_ || o.is_null_) return false;

    if (type_id_ == o.type_id_) {
        switch (type_id_) {
            case TypeId::BOOLEAN:
                return std::get<bool>(val_) == std::get<bool>(o.val_);
            case TypeId::INTEGER:
                return std::get<int32_t>(val_) == std::get<int32_t>(o.val_);
            case TypeId::BIGINT:
                return std::get<int64_t>(val_) == std::get<int64_t>(o.val_);
            case TypeId::DOUBLE:
                return std::get<double>(val_) == std::get<double>(o.val_);
            case TypeId::VARCHAR:
                return std::get<std::string>(val_) == std::get<std::string>(o.val_);
            default:
                return false;
        }
    }

    // Coercion comparisons for numeric types
    if ((type_id_ == TypeId::INTEGER || type_id_ == TypeId::BIGINT || type_id_ == TypeId::DOUBLE) &&
        (o.type_id_ == TypeId::INTEGER || o.type_id_ == TypeId::BIGINT || o.type_id_ == TypeId::DOUBLE)) {
        return GetAsDouble() == o.GetAsDouble();
    }

    return false;
}

bool Value::operator<(const Value& o) const {
    if (is_null_ && o.is_null_) return false;
    if (is_null_) return true; // NULLs first
    if (o.is_null_) return false;

    if (type_id_ == o.type_id_) {
        switch (type_id_) {
            case TypeId::BOOLEAN:
                return std::get<bool>(val_) < std::get<bool>(o.val_);
            case TypeId::INTEGER:
                return std::get<int32_t>(val_) < std::get<int32_t>(o.val_);
            case TypeId::BIGINT:
                return std::get<int64_t>(val_) < std::get<int64_t>(o.val_);
            case TypeId::DOUBLE:
                return std::get<double>(val_) < std::get<double>(o.val_);
            case TypeId::VARCHAR:
                return std::get<std::string>(val_) < std::get<std::string>(o.val_);
            default:
                return false;
        }
    }

    // Numeric comparison
    if ((type_id_ == TypeId::INTEGER || type_id_ == TypeId::BIGINT || type_id_ == TypeId::DOUBLE) &&
        (o.type_id_ == TypeId::INTEGER || o.type_id_ == TypeId::BIGINT || o.type_id_ == TypeId::DOUBLE)) {
        return GetAsDouble() < o.GetAsDouble();
    }

    return type_id_ < o.type_id_;
}

} // namespace emberdb
