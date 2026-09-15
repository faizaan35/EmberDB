#pragma once

#include "emberdb/common/types.h"
#include <string>

namespace emberdb {

class Column {
public:
    Column() : name_(""), type_(TypeId::INVALID), length_(0), nullable_(true) {}
    Column(std::string name, TypeId type, bool nullable = true);
    Column(std::string name, TypeId type, uint32_t length, bool nullable = true);
    Column(std::string name, TypeId type, int length, bool nullable = true);

    const std::string& GetName() const { return name_; }
    TypeId GetType() const { return type_; }
    uint32_t GetLength() const { return length_; }
    bool IsNullable() const { return nullable_; }

    bool IsInlined() const { return type_ != TypeId::VARCHAR; }
    uint32_t GetFixedSize() const;

    std::string ToString() const;

    // Serialization for catalog
    size_t GetSerializedSize() const;
    void SerializeTo(char* dest) const;
    static Column DeserializeFrom(const char* src, size_t& bytes_read);

    bool operator==(const Column& o) const {
        return name_ == o.name_ && type_ == o.type_ && length_ == o.length_ && nullable_ == o.nullable_;
    }

private:
    std::string name_;
    TypeId type_;
    uint32_t length_;
    bool nullable_;
};

} // namespace emberdb
