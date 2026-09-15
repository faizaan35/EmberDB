#pragma once

#include "emberdb/catalog/catalog.h"
#include "emberdb/sql/ast/ast.h"
#include "emberdb/storage/record/record.h"
#include <vector>
#include <string>
#include <chrono>

namespace emberdb {

struct QueryResult {
    bool success{true};
    std::string error_message;
    Schema schema;
    std::vector<Record> rows;
    uint32_t rows_affected{0};
    double execution_time_ms{0.0};

    std::string FormatAsTable() const;
};

class ExecutionEngine {
public:
    explicit ExecutionEngine(Catalog* catalog);

    QueryResult Execute(const Statement* stmt);

private:
    QueryResult ExecuteCreateTable(const CreateTableStatement* stmt);
    QueryResult ExecuteDropTable(const DropTableStatement* stmt);
    QueryResult ExecuteCreateIndex(const CreateIndexStatement* stmt);
    QueryResult ExecuteInsert(const InsertStatement* stmt);
    QueryResult ExecuteSelect(const SelectStatement* stmt);
    QueryResult ExecuteUpdate(const UpdateStatement* stmt);
    QueryResult ExecuteDelete(const DeleteStatement* stmt);
    QueryResult ExecuteTransaction(const TransactionStatement* stmt);

    Catalog* catalog_;
};

} // namespace emberdb
