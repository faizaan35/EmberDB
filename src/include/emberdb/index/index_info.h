#pragma once

#include "emberdb/common/types.h"
#include "emberdb/index/btree/b_plus_tree_index.h"
#include <string>
#include <memory>

namespace emberdb {

/**
 * IndexInfo stores metadata and the BPlusTreeIndex instance for a table column.
 */
class IndexInfo {
public:
    IndexInfo(std::string index_name,
              std::string table_name,
              std::string column_name,
              uint32_t column_idx,
              TypeId key_type,
              std::unique_ptr<BPlusTreeIndex> index)
        : index_name_(std::move(index_name)),
          table_name_(std::move(table_name)),
          column_name_(std::move(column_name)),
          column_idx_(column_idx),
          key_type_(key_type),
          index_(std::move(index)) {}

    const std::string& GetIndexName() const { return index_name_; }
    const std::string& GetTableName() const { return table_name_; }
    const std::string& GetColumnName() const { return column_name_; }
    uint32_t GetColumnIdx() const { return column_idx_; }
    TypeId GetKeyType() const { return key_type_; }

    BPlusTreeIndex* GetIndex() { return index_.get(); }
    const BPlusTreeIndex* GetIndex() const { return index_.get(); }

private:
    std::string index_name_;
    std::string table_name_;
    std::string column_name_;
    uint32_t column_idx_{0};
    TypeId key_type_{TypeId::INVALID};
    std::unique_ptr<BPlusTreeIndex> index_;
};

} // namespace emberdb
