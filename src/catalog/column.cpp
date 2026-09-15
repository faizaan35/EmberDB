#include "emberdb/catalog/column.h"

namespace emberdb {

Column::Column(std::string name, TypeId type, bool nullable)
    : name_(std::move(name)), type_(type), length_(0), nullable_(nullable) {
    switch (type_) {
        case TypeId::BOOLEAN:
            length_ = sizeof(bool);
            break;
        case TypeId::INTEGER:
            length_ = sizeof(int32_t);
            break;
        case TypeId::BIGINT:
            length_ = sizeof(int64_t);
            break;
        case TypeId::DOUBLE:
            length_ = sizeof(double);
            break;
        case TypeId::VARCHAR:
            length_ = 255; // Default max length
            break;
        default:
            length_ = 0;
            break;
    }
}

Column::Column(std::string name, TypeId type, uint32_t length, bool nullable)
    : name_(std::move(name)), type_(type), length_(length), nullable_(nullable) {}

Column::Column(std::string name, TypeId type, int length, bool nullable)
    : name_(std::move(name)), type_(type), length_(static_cast<uint32_t>(length)), nullable_(nullable) {}

uint32_t Column::GetFixedSize() const {
    switch (type_) {
        case TypeId::BOOLEAN: return sizeof(bool);
        case TypeId::INTEGER: return sizeof(int32_t);
        case TypeId::BIGINT:  return sizeof(int64_t);
        case TypeId::DOUBLE:  return sizeof(double);
        case TypeId::VARCHAR: return sizeof(uint32_t); // Offset pointer
        default: return 0;
    }
}

std::string Column::ToString() const {
    std::string str = name_ + " " + TypeIdToString(type_);
    if (type_ == TypeId::VARCHAR) {
        str += "(" + std::to_string(length_) + ")";
    }
    if (!nullable_) {
        str += " NOT NULL";
    }
    return str;
}

size_t Column::GetSerializedSize() const {
    // uint16_t name_len + name + 1 byte type + 4 bytes length + 1 byte nullable
    return sizeof(uint16_t) + name_.size() + 1 + sizeof(uint32_t) + 1;
}

void Column::SerializeTo(char* dest) const {
    uint16_t name_len = static_cast<uint16_t>(name_.size());
    std::memcpy(dest, &name_len, sizeof(name_len));
    dest += sizeof(name_len);

    std::memcpy(dest, name_.data(), name_len);
    dest += name_len;

    uint8_t t = static_cast<uint8_t>(type_);
    std::memcpy(dest, &t, 1);
    dest += 1;

    std::memcpy(dest, &length_, sizeof(length_));
    dest += sizeof(length_);

    uint8_t n = nullable_ ? 1 : 0;
    std::memcpy(dest, &n, 1);
}

Column Column::DeserializeFrom(const char* src, size_t& bytes_read) {
    uint16_t name_len = 0;
    std::memcpy(&name_len, src, sizeof(name_len));
    src += sizeof(name_len);
    bytes_read = sizeof(name_len);

    std::string name(src, name_len);
    src += name_len;
    bytes_read += name_len;

    uint8_t t = 0;
    std::memcpy(&t, src, 1);
    src += 1;
    bytes_read += 1;

    uint32_t length = 0;
    std::memcpy(&length, src, sizeof(length));
    src += sizeof(length);
    bytes_read += sizeof(length);

    uint8_t n = 0;
    std::memcpy(&n, src, 1);
    bytes_read += 1;

    return Column(std::move(name), static_cast<TypeId>(t), length, n != 0);
}

} // namespace emberdb
