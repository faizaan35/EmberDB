#include "forgedb/execution/executor/execution_engine.h"
#include "forgedb/execution/executor/seq_scan_executor.h"
#include "forgedb/execution/executor/projection_executor.h"
#include "forgedb/execution/expressions/expression_evaluator.h"
#include <iomanip>
#include <sstream>

namespace forgedb {

ExecutionEngine::ExecutionEngine(Catalog* catalog) : catalog_(catalog) {}

QueryResult ExecutionEngine::Execute(const Statement* stmt) {
    if (!stmt) {
        return QueryResult{false, "Null statement", {}, {}, 0, 0.0};
    }

    auto start_time = std::chrono::high_resolution_clock::now();
    QueryResult result;

    switch (stmt->GetType()) {
        case StatementType::CREATE_TABLE:
            result = ExecuteCreateTable(dynamic_cast<const CreateTableStatement*>(stmt));
            break;
        case StatementType::DROP_TABLE:
            result = ExecuteDropTable(dynamic_cast<const DropTableStatement*>(stmt));
            break;
        case StatementType::CREATE_INDEX:
            result = ExecuteCreateIndex(dynamic_cast<const CreateIndexStatement*>(stmt));
            break;
        case StatementType::INSERT:
            result = ExecuteInsert(dynamic_cast<const InsertStatement*>(stmt));
            break;
        case StatementType::SELECT:
            result = ExecuteSelect(dynamic_cast<const SelectStatement*>(stmt));
            break;
        case StatementType::UPDATE:
            result = ExecuteUpdate(dynamic_cast<const UpdateStatement*>(stmt));
            break;
        case StatementType::DELETE:
            result = ExecuteDelete(dynamic_cast<const DeleteStatement*>(stmt));
            break;
        case StatementType::TRANSACTION:
            result = ExecuteTransaction(dynamic_cast<const TransactionStatement*>(stmt));
            break;
        default:
            result = QueryResult{false, "Unsupported statement type", {}, {}, 0, 0.0};
            break;
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    result.execution_time_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
    return result;
}

QueryResult ExecutionEngine::ExecuteCreateTable(const CreateTableStatement* stmt) {
    std::vector<Column> cols;
    for (const auto& cdef : stmt->GetColumns()) {
        cols.emplace_back(cdef.name, cdef.type, cdef.length, cdef.nullable);
    }
    Schema schema(std::move(cols));

    auto res = catalog_->CreateTable(stmt->GetTableName(), schema);
    if (!res.ok()) {
        return QueryResult{false, res.status().ToString(), {}, {}, 0, 0.0};
    }

    return QueryResult{true, "", {}, {}, 0, 0.0};
}

QueryResult ExecutionEngine::ExecuteDropTable(const DropTableStatement* stmt) {
    if (!catalog_->HasTable(stmt->GetTableName())) {
        return QueryResult{false, "Table not found: " + stmt->GetTableName(), {}, {}, 0, 0.0};
    }
    // Drop table metadata
    return QueryResult{true, "", {}, {}, 0, 0.0};
}

QueryResult ExecutionEngine::ExecuteCreateIndex(const CreateIndexStatement* stmt) {
    if (!catalog_->HasTable(stmt->GetTableName())) {
        return QueryResult{false, "Table not found: " + stmt->GetTableName(), {}, {}, 0, 0.0};
    }
    return QueryResult{true, "", {}, {}, 0, 0.0};
}

QueryResult ExecutionEngine::ExecuteInsert(const InsertStatement* stmt) {
    Table* table = catalog_->GetTable(stmt->GetTableName());
    if (!table) {
        return QueryResult{false, "Table not found: " + stmt->GetTableName(), {}, {}, 0, 0.0};
    }

    const Schema& schema = table->GetSchema();
    uint32_t rows_inserted = 0;

    for (const auto& row_exprs : stmt->GetValues()) {
        std::vector<Value> row_values;

        if (stmt->GetColumns().empty()) {
            // Values mapped directly by position
            if (row_exprs.size() != schema.GetColumnCount()) {
                return QueryResult{false, "Column count mismatch in INSERT: expected " +
                                           std::to_string(schema.GetColumnCount()) +
                                           ", got " + std::to_string(row_exprs.size()), {}, {}, 0, 0.0};
            }
            for (const auto& expr : row_exprs) {
                row_values.push_back(ExpressionEvaluator::Evaluate(expr.get()));
            }
        } else {
            // Values mapped by column name list
            if (row_exprs.size() != stmt->GetColumns().size()) {
                return QueryResult{false, "Column count does not match values count in INSERT", {}, {}, 0, 0.0};
            }
            // Initialize with NULLs
            row_values.resize(schema.GetColumnCount());
            for (size_t i = 0; i < schema.GetColumnCount(); ++i) {
                row_values[i] = Value::Null(schema.GetColumn(i).GetType());
            }

            for (size_t i = 0; i < stmt->GetColumns().size(); ++i) {
                uint32_t col_idx = schema.GetColIdx(stmt->GetColumns()[i]);
                row_values[col_idx] = ExpressionEvaluator::Evaluate(row_exprs[i].get());
            }
        }

        Record record(std::move(row_values), schema);
        auto status = table->GetTableHeap()->InsertRecord(record);
        if (!status.ok()) {
            return QueryResult{false, status.ToString(), {}, {}, rows_inserted, 0.0};
        }
        ++rows_inserted;
    }

    return QueryResult{true, "", {}, {}, rows_inserted, 0.0};
}

QueryResult ExecutionEngine::ExecuteSelect(const SelectStatement* stmt) {
    Table* table = catalog_->GetTable(stmt->GetFromTable());
    if (!table) {
        return QueryResult{false, "Table not found: " + stmt->GetFromTable(), {}, {}, 0, 0.0};
    }

    SeqScanExecutor scan(table, stmt->GetWhereClause());
    scan.Init();

    std::vector<Record> results;
    Schema output_schema;

    bool is_star = (stmt->GetSelectList().size() == 1 &&
                    stmt->GetSelectList()[0]->GetType() == ExpressionType::STAR);

    if (is_star) {
        output_schema = table->GetSchema();
        Record rec;
        RID rid;
        while (scan.Next(&rec, &rid)) {
            results.push_back(std::move(rec));
            if (stmt->GetLimit().has_value() && results.size() >= static_cast<size_t>(stmt->GetLimit().value())) {
                break;
            }
        }
    } else {
        // Build projected output schema
        std::vector<Column> proj_cols;
        std::vector<const Expression*> proj_exprs;

        for (const auto& expr : stmt->GetSelectList()) {
            proj_exprs.push_back(expr.get());
            if (expr->GetType() == ExpressionType::COLUMN_REF) {
                const auto* col_ref = dynamic_cast<const ColumnRefExpression*>(expr.get());
                uint32_t col_idx = table->GetSchema().GetColIdx(col_ref->GetColumnName());
                proj_cols.push_back(table->GetSchema().GetColumn(col_idx));
            } else {
                proj_cols.emplace_back(expr->ToString(), TypeId::VARCHAR, 255);
            }
        }

        output_schema = Schema(std::move(proj_cols));
        ProjectionExecutor proj(&scan, std::move(proj_exprs), output_schema);
        proj.Init();

        Record rec;
        RID rid;
        while (proj.Next(&rec, &rid)) {
            results.push_back(std::move(rec));
            if (stmt->GetLimit().has_value() && results.size() >= static_cast<size_t>(stmt->GetLimit().value())) {
                break;
            }
        }
    }

    return QueryResult{true, "", std::move(output_schema), std::move(results), 0, 0.0};
}

QueryResult ExecutionEngine::ExecuteUpdate(const UpdateStatement* stmt) {
    Table* table = catalog_->GetTable(stmt->GetTableName());
    if (!table) {
        return QueryResult{false, "Table not found: " + stmt->GetTableName(), {}, {}, 0, 0.0};
    }

    const Schema& schema = table->GetSchema();

    // First scan to collect records and RIDs to update
    SeqScanExecutor scan(table, stmt->GetWhereClause());
    scan.Init();

    std::vector<std::pair<RID, Record>> to_update;
    Record rec;
    RID rid;
    while (scan.Next(&rec, &rid)) {
        to_update.emplace_back(rid, std::move(rec));
    }

    uint32_t updated_count = 0;
    for (auto& pair : to_update) {
        RID curr_rid = pair.first;
        Record& current_record = pair.second;

        auto vals = current_record.GetValues(schema);

        // Apply assignments
        for (const auto& assign : stmt->GetAssignments()) {
            uint32_t col_idx = schema.GetColIdx(assign.first);
            Value new_val = ExpressionEvaluator::Evaluate(assign.second.get(), &current_record, &schema);
            vals[col_idx] = std::move(new_val);
        }

        Record new_record(std::move(vals), schema);
        auto status = table->GetTableHeap()->UpdateRecord(curr_rid, new_record);
        if (!status.ok()) {
            return QueryResult{false, status.ToString(), {}, {}, updated_count, 0.0};
        }
        ++updated_count;
    }

    return QueryResult{true, "", {}, {}, updated_count, 0.0};
}

QueryResult ExecutionEngine::ExecuteDelete(const DeleteStatement* stmt) {
    Table* table = catalog_->GetTable(stmt->GetTableName());
    if (!table) {
        return QueryResult{false, "Table not found: " + stmt->GetTableName(), {}, {}, 0, 0.0};
    }

    SeqScanExecutor scan(table, stmt->GetWhereClause());
    scan.Init();

    std::vector<RID> to_delete;
    Record rec;
    RID rid;
    while (scan.Next(&rec, &rid)) {
        to_delete.push_back(rid);
    }

    uint32_t deleted_count = 0;
    for (const auto& r : to_delete) {
        auto status = table->GetTableHeap()->DeleteRecord(r);
        if (!status.ok()) {
            return QueryResult{false, status.ToString(), {}, {}, deleted_count, 0.0};
        }
        ++deleted_count;
    }

    return QueryResult{true, "", {}, {}, deleted_count, 0.0};
}

QueryResult ExecutionEngine::ExecuteTransaction(const TransactionStatement* /*stmt*/) {
    return QueryResult{true, "", {}, {}, 0, 0.0};
}

std::string QueryResult::FormatAsTable() const {
    if (!success) {
        return "ERROR: " + error_message + "\n";
    }

    if (schema.GetColumnCount() == 0) {
        return "Query OK, " + std::to_string(rows_affected) + " row(s) affected.\n";
    }

    std::vector<std::string> headers;
    std::vector<size_t> col_widths;

    for (uint32_t i = 0; i < schema.GetColumnCount(); ++i) {
        std::string h = schema.GetColumn(i).GetName();
        col_widths.push_back(h.size());
        headers.push_back(std::move(h));
    }

    // Extract all rows as string grids to calculate column widths
    std::vector<std::vector<std::string>> grid;
    grid.reserve(rows.size());
    for (const auto& row : rows) {
        auto vals = row.GetValues(schema);
        std::vector<std::string> row_strs;
        row_strs.reserve(vals.size());
        for (size_t i = 0; i < vals.size(); ++i) {
            std::string s = vals[i].ToString();
            if (s.size() > col_widths[i]) {
                col_widths[i] = s.size();
            }
            row_strs.push_back(std::move(s));
        }
        grid.push_back(std::move(row_strs));
    }

    // Minimum column width is 3
    for (auto& w : col_widths) {
        if (w < 3) w = 3;
    }

    std::ostringstream ss;

    // Top border
    ss << "+";
    for (size_t w : col_widths) {
        ss << std::string(w + 2, '-') << "+";
    }
    ss << "\n";

    // Header row
    ss << "|";
    for (size_t i = 0; i < headers.size(); ++i) {
        ss << " " << std::left << std::setw(static_cast<int>(col_widths[i])) << headers[i] << " |";
    }
    ss << "\n";

    // Header separator
    ss << "+";
    for (size_t w : col_widths) {
        ss << std::string(w + 2, '-') << "+";
    }
    ss << "\n";

    // Data rows
    for (const auto& row : grid) {
        ss << "|";
        for (size_t i = 0; i < row.size(); ++i) {
            ss << " " << std::left << std::setw(static_cast<int>(col_widths[i])) << row[i] << " |";
        }
        ss << "\n";
    }

    // Bottom border
    ss << "+";
    for (size_t w : col_widths) {
        ss << std::string(w + 2, '-') << "+";
    }
    ss << "\n";

    ss << rows.size() << " row(s) in set.\n";
    return ss.str();
}

} // namespace forgedb
