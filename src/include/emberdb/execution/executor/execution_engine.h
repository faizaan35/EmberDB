#pragma once

#include "emberdb/catalog/catalog.h"
#include "emberdb/planner/planner.h"
#include "emberdb/sql/ast/ast.h"
#include "emberdb/storage/record/record.h"
#include "emberdb/transaction/transaction_manager.h"
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
    explicit ExecutionEngine(Catalog* catalog, class LogManager* log_mgr = nullptr);

    QueryResult Execute(const Statement* stmt, Transaction* txn = nullptr);

    Planner* GetPlanner() { return &planner_; }
    const Planner* GetPlanner() const { return &planner_; }

    TransactionManager* GetTransactionManager() { return &txn_mgr_; }
    Transaction* GetActiveTransaction() const { return active_txn_.get(); }

    void SetLogManager(class LogManager* log_mgr);
    class LogManager* GetLogManager() const { return log_mgr_; }

private:
    QueryResult ExecuteCreateTable(const CreateTableStatement* stmt);
    QueryResult ExecuteDropTable(const DropTableStatement* stmt);
    QueryResult ExecuteCreateIndex(const CreateIndexStatement* stmt);
    QueryResult ExecuteInsert(const InsertStatement* stmt, Transaction* txn);
    QueryResult ExecuteSelect(const SelectStatement* stmt);
    QueryResult ExecuteUpdate(const UpdateStatement* stmt, Transaction* txn);
    QueryResult ExecuteDelete(const DeleteStatement* stmt, Transaction* txn);
    QueryResult ExecuteTransaction(const TransactionStatement* stmt);
    QueryResult ExecuteExplain(const ExplainStatement* stmt);

    Catalog* catalog_;
    class LogManager* log_mgr_{nullptr};
    Planner planner_;
    TransactionManager txn_mgr_;
    std::shared_ptr<Transaction> active_txn_{nullptr};
};

} // namespace emberdb
