#pragma once

#include "forgedb/catalog/column.h"
#include <vector>
#include <unordered_map>
#include <stdexcept>

namespace forgedb {

class Schema {
public:
    Schema() = default;
    explicit Schema(std::vector<Column> columns);

    const std::vector<Column>& GetColumns() const { return columns_; }
    uint32_t GetColumnCount() const { return static_cast<uint32_t>(columns_.size()); }
    const Column& GetColumn(uint32_t col_idx) const;
    uint32_t GetColIdx(const std::string& col_name) const;
    bool HasColumn(const std::string& col_name) const;

    std::string ToString() const;

    size_t GetSerializedSize() const;
    void SerializeTo(char* dest) const;
    static Schema DeserializeFrom(const char* src, size_t& bytes_read);

    bool operator==(const Schema& o) const {
        return columns_ == o.columns_;
    }

private:
    std::vector<Column> columns_;
    std::unordered_map<std::string, uint32_t> name_to_idx_;
};

} // namespace forgedb
