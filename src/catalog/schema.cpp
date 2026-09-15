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
    if (it != name_to_idx_.end()) {
        return it->second;
    }

    size_t dot_pos = col_name.find('.');
    if (dot_pos == std::string::npos) {
        // Unqualified name, look for unique suffix match (e.g. "age" matching "users.age")
        std::string suffix = "." + col_name;
        int found_idx = -1;
        int match_count = 0;
        for (size_t i = 0; i < columns_.size(); ++i) {
            const std::string& cname = columns_[i].GetName();
            if (cname == col_name ||
                (cname.size() > suffix.size() &&
                 cname.compare(cname.size() - suffix.size(), suffix.size(), suffix) == 0)) {
                found_idx = static_cast<int>(i);
                match_count++;
            }
        }
        if (match_count == 1) {
            return static_cast<uint32_t>(found_idx);
        }
        if (match_count > 1) {
            throw std::runtime_error("Ambiguous column reference: " + col_name);
        }
    } else {
        // Qualified name like "users.age", check if columns_ has unqualified "age"
        std::string unqualified = col_name.substr(dot_pos + 1);
        auto it_unq = name_to_idx_.find(unqualified);
        if (it_unq != name_to_idx_.end()) {
            return it_unq->second;
        }
    }

    throw std::runtime_error("Column not found in schema: " + col_name);
}

bool Schema::HasColumn(const std::string& col_name) const {
    try {
        GetColIdx(col_name);
        return true;
    } catch (...) {
        return false;
    }
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
