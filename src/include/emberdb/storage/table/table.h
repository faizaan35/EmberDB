#pragma once

#include "emberdb/catalog/schema.h"
#include "emberdb/storage/table/table_heap.h"
#include <memory>
#include <string>

namespace emberdb {

/**
 * Table encapsulates a named relation, its schema, and its physical on-disk TableHeap.
 */
class Table {
public:
    Table(std::string name, Schema schema, std::unique_ptr<TableHeap> table_heap)
        : name_(std::move(name)), schema_(std::move(schema)), table_heap_(std::move(table_heap)) {}

    const std::string& GetName() const { return name_; }
    const Schema& GetSchema() const { return schema_; }

    TableHeap* GetTableHeap() { return table_heap_.get(); }
    const TableHeap* GetTableHeap() const { return table_heap_.get(); }
    page_id_t GetFirstPageId() const { return table_heap_->GetFirstPageId(); }

private:
    std::string name_;
    Schema schema_;
    std::unique_ptr<TableHeap> table_heap_;
};

} // namespace emberdb
