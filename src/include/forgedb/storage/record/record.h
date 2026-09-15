#pragma once

#include "forgedb/common/config.h"
#include "forgedb/common/types.h"
#include "forgedb/catalog/schema.h"
#include <vector>
#include <cstring>
#include <string>

namespace forgedb {

/**
 * Record represents a serialized physical tuple stored inside a page slot.
 */
class Record {
public:
    Record() = default;
    Record(std::vector<Value> values, const Schema& schema);
    Record(const char* data, uint32_t size, RID rid = RID{});

    RID GetRID() const { return rid_; }
    void SetRID(RID rid) { rid_ = rid; }

    uint32_t GetLength() const { return static_cast<uint32_t>(data_.size()); }
    const char* GetData() const { return data_.data(); }
    char* GetDataMut() { return data_.data(); }

    Value GetValue(const Schema& schema, uint32_t col_idx) const;
    std::vector<Value> GetValues(const Schema& schema) const;

    std::string ToString(const Schema& schema) const;

    bool operator==(const Record& o) const {
        return data_ == o.data_;
    }

private:
    RID rid_{};
    std::vector<char> data_;
};

} // namespace forgedb
