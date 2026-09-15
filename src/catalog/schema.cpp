#include "forgedb/catalog/schema.h"

namespace forgedb {

Schema::Schema(std::vector<Column> columns) : columns_(std::move(columns)) {
    for (uint32_t i = 0; i < columns_.size(); ++i) {
        name_to_idx_[columns_[i].GetName()] = i;
    }
}

const Column& Schema::GetColumn(uint32_t col_idx) const {
    if (col_idx >= columns_.size()) {
        throw std::out_of_range("Column index out of range: " + std::to_string(col_idx));
    }
    return columns_[col_idx];
}

uint32_t Schema::GetColIdx(const std::string& col_name) const {
    auto it = name_to_idx_.find(col_name);
    if (it == name_to_idx_.end()) {
        throw std::runtime_error("Column not found in schema: " + col_name);
    }
    return it->second;
}

bool Schema::HasColumn(const std::string& col_name) const {
    return name_to_idx_.find(col_name) != name_to_idx_.end();
}

std::string Schema::ToString() const {
    std::string str = "(";
    for (size_t i = 0; i < columns_.size(); ++i) {
        if (i > 0) str += ", ";
        str += columns_[i].ToString();
    }
    str += ")";
    return str;
}

size_t Schema::GetSerializedSize() const {
    size_t size = sizeof(uint32_t); // column count
    for (const auto& col : columns_) {
        size += col.GetSerializedSize();
    }
    return size;
}

void Schema::SerializeTo(char* dest) const {
    uint32_t count = static_cast<uint32_t>(columns_.size());
    std::memcpy(dest, &count, sizeof(count));
    dest += sizeof(count);

    for (const auto& col : columns_) {
        col.SerializeTo(dest);
        dest += col.GetSerializedSize();
    }
}

Schema Schema::DeserializeFrom(const char* src, size_t& bytes_read) {
    uint32_t count = 0;
    std::memcpy(&count, src, sizeof(count));
    src += sizeof(count);
    bytes_read = sizeof(count);

    std::vector<Column> cols;
    cols.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        size_t col_bytes = 0;
        cols.push_back(Column::DeserializeFrom(src, col_bytes));
        src += col_bytes;
        bytes_read += col_bytes;
    }

    return Schema(std::move(cols));
}

} // namespace forgedb
