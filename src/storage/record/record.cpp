#include "emberdb/storage/record/record.h"
#include <stdexcept>

namespace emberdb {

Record::Record(std::vector<Value> values, const Schema& schema) {
    if (values.size() != schema.GetColumnCount()) {
        throw std::runtime_error("Value count does not match schema column count");
    }

    size_t col_count = schema.GetColumnCount();
    for (size_t i = 0; i < col_count; ++i) {
        if (!values[i].IsNull() && values[i].GetTypeId() != schema.GetColumn(i).GetType()) {
            values[i] = values[i].CastAs(schema.GetColumn(i).GetType());
        }
    }

    size_t null_bitmap_size = (col_count + 7) / 8;

    // Calculate total size needed
    size_t total_size = sizeof(uint32_t) + null_bitmap_size;
    for (const auto& v : values) {
        total_size += v.GetSerializedSize();
    }

    data_.resize(total_size);
    char* dest = data_.data();

    // Write total length
    uint32_t len = static_cast<uint32_t>(total_size);
    std::memcpy(dest, &len, sizeof(len));
    dest += sizeof(len);

    // Build and write null bitmap
    std::vector<uint8_t> null_bitmap(null_bitmap_size, 0);
    for (size_t i = 0; i < col_count; ++i) {
        if (values[i].IsNull()) {
            null_bitmap[i / 8] |= (1 << (i % 8));
        }
    }
    std::memcpy(dest, null_bitmap.data(), null_bitmap_size);
    dest += null_bitmap_size;

    // Write values
    for (const auto& v : values) {
        v.SerializeTo(dest);
        dest += v.GetSerializedSize();
    }
}

Record::Record(const char* data, uint32_t size, RID rid) : rid_(rid) {
    data_.assign(data, data + size);
}

Value Record::GetValue(const Schema& schema, uint32_t col_idx) const {
    if (col_idx >= schema.GetColumnCount()) {
        throw std::out_of_range("Column index out of range");
    }

    if (data_.empty()) {
        throw std::runtime_error("Cannot get value from empty record");
    }

    size_t col_count = schema.GetColumnCount();
    size_t null_bitmap_size = (col_count + 7) / 8;

    const char* src = data_.data();
    src += sizeof(uint32_t); // skip total_size
    src += null_bitmap_size; // skip null bitmap

    // Walk columns up to col_idx
    for (uint32_t i = 0; i < schema.GetColumnCount(); ++i) {
        size_t bytes_read = 0;
        TypeId t = schema.GetColumn(i).GetType();
        Value val = Value::DeserializeFrom(src, t, bytes_read);
        src += bytes_read;

        if (i == col_idx) {
            return val;
        }
    }

    throw std::runtime_error("Failed to extract column value");
}

std::vector<Value> Record::GetValues(const Schema& schema) const {
    std::vector<Value> values;
    values.reserve(schema.GetColumnCount());

    size_t col_count = schema.GetColumnCount();
    size_t null_bitmap_size = (col_count + 7) / 8;

    const char* src = data_.data();
    src += sizeof(uint32_t); // skip total_size
    src += null_bitmap_size; // skip null bitmap

    for (uint32_t i = 0; i < col_count; ++i) {
        size_t bytes_read = 0;
        TypeId t = schema.GetColumn(i).GetType();
        values.push_back(Value::DeserializeFrom(src, t, bytes_read));
        src += bytes_read;
    }

    return values;
}

std::string Record::ToString(const Schema& schema) const {
    auto vals = GetValues(schema);
    std::string str = "(";
    for (size_t i = 0; i < vals.size(); ++i) {
        if (i > 0) str += ", ";
        str += vals[i].ToString();
    }
    str += ")";
    return str;
}

} // namespace emberdb
