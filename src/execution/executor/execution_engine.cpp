#include "forgedb/execution/executor/execution_engine.h"
#include "forgedb/execution/executor/seq_scan_executor.h"
#include "forgedb/execution/executor/projection_executor.h"
#include "forgedb/execution/executor/sort_executor.h"
#include "forgedb/execution/executor/limit_executor.h"
#include "forgedb/execution/executor/aggregate_executor.h"
#include "forgedb/execution/executor/filter_executor.h"
#include "forgedb/execution/executor/nested_loop_join_executor.h"
#include "forgedb/execution/expressions/expression_evaluator.h"
#include <iomanip>
#include <sstream>
#include <algorithm>

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
            if (row_exprs.size() != schema.GetColumnCount()) {
                return QueryResult{false, "Column count mismatch in INSERT: expected " +
                                           std::to_string(schema.GetColumnCount()) +
                                           ", got " + std::to_string(row_exprs.size()), {}, {}, 0, 0.0};
            }
            for (const auto& expr : row_exprs) {
                row_values.push_back(ExpressionEvaluator::Evaluate(expr.get()));
            }
        } else {
            if (row_exprs.size() != stmt->GetColumns().size()) {
                return QueryResult{false, "Column count does not match values count in INSERT", {}, {}, 0, 0.0};
            }
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

    // 1. Base scan and joins setup
    std::unique_ptr<SeqScanExecutor> base_scan;
    std::vector<std::unique_ptr<SeqScanExecutor>> join_scans;
    std::vector<std::unique_ptr<NestedLoopJoinExecutor>> join_execs;
    std::unique_ptr<FilterExecutor> filter_exec;

    AbstractExecutor* current_head = nullptr;

    if (stmt->GetJoins().empty()) {
        base_scan = std::make_unique<SeqScanExecutor>(table, stmt->GetWhereClause());
        current_head = base_scan.get();
    } else {
        // Build table-qualified schema for left table
        std::vector<Column> left_cols;
        for (const auto& col : table->GetSchema().GetColumns()) {
            left_cols.emplace_back(stmt->GetFromTable() + "." + col.GetName(), col.GetType(), col.GetLength(), col.IsNullable());
        }
        Schema left_schema(std::move(left_cols));
        base_scan = std::make_unique<SeqScanExecutor>(table, nullptr, std::move(left_schema));
        current_head = base_scan.get();

        for (const auto& join_def : stmt->GetJoins()) {
            Table* right_table = catalog_->GetTable(join_def.table_name);
            if (!right_table) {
                return QueryResult{false, "Table not found in JOIN: " + join_def.table_name, {}, {}, 0, 0.0};
            }
            std::vector<Column> right_cols;
            for (const auto& col : right_table->GetSchema().GetColumns()) {
                right_cols.emplace_back(join_def.table_name + "." + col.GetName(), col.GetType(), col.GetLength(), col.IsNullable());
            }
            Schema right_schema(std::move(right_cols));
            auto right_scan = std::make_unique<SeqScanExecutor>(right_table, nullptr, std::move(right_schema));

            // Combined output schema
            std::vector<Column> joined_cols;
            for (const auto& c : current_head->GetOutputSchema().GetColumns()) {
                joined_cols.push_back(c);
            }
            for (const auto& c : right_scan->GetOutputSchema().GetColumns()) {
                joined_cols.push_back(c);
            }
            Schema joined_schema(std::move(joined_cols));

            auto join_exec = std::make_unique<NestedLoopJoinExecutor>(
                current_head, right_scan.get(), join_def.type, join_def.on_condition.get(), std::move(joined_schema));

            current_head = join_exec.get();
            join_scans.push_back(std::move(right_scan));
            join_execs.push_back(std::move(join_exec));
        }

        // Apply WHERE filter after joins if present
        if (stmt->GetWhereClause()) {
            filter_exec = std::make_unique<FilterExecutor>(current_head, stmt->GetWhereClause());
            current_head = filter_exec.get();
        }
    }

    // 2. Check if aggregation is involved
    bool has_aggregation = !stmt->GetGroupBy().empty();
    if (!has_aggregation) {
        for (const auto& expr : stmt->GetSelectList()) {
            if (expr->GetType() == ExpressionType::FUNCTION_CALL) {
                has_aggregation = true;
                break;
            }
        }
    }

    // Storage for intermediate executors
    std::unique_ptr<AggregateExecutor> agg_exec;
    std::unique_ptr<SortExecutor> sort_exec;
    std::unique_ptr<LimitExecutor> limit_exec;
    std::unique_ptr<ProjectionExecutor> proj_exec;

    if (has_aggregation) {
        std::vector<const Expression*> group_by_exprs;
        for (const auto& ge : stmt->GetGroupBy()) {
            group_by_exprs.push_back(ge.get());
        }

        std::vector<AggregateDef> agg_defs;
        std::vector<Column> agg_out_cols;

        // Group by columns in output schema
        for (const auto* ge : group_by_exprs) {
            if (ge->GetType() == ExpressionType::COLUMN_REF) {
                const auto* cr = dynamic_cast<const ColumnRefExpression*>(ge);
                std::string col_lookup = cr->GetColumnName();
                if (!cr->GetTableName().empty()) {
                    col_lookup = cr->GetTableName() + "." + cr->GetColumnName();
                }
                uint32_t cidx = current_head->GetOutputSchema().GetColIdx(col_lookup);
                agg_out_cols.push_back(current_head->GetOutputSchema().GetColumn(cidx));
            } else {
                agg_out_cols.emplace_back(ge->ToString(), TypeId::VARCHAR, 255);
            }
        }

        // Aggregate functions
        for (const auto& expr : stmt->GetSelectList()) {
            if (expr->GetType() == ExpressionType::FUNCTION_CALL) {
                const auto* fc = dynamic_cast<const FunctionCallExpression*>(expr.get());
                std::string fname = fc->GetFunctionName();

                AggregateType atype = AggregateType::COUNT;
                TypeId rtype = TypeId::BIGINT;

                if (fname == "COUNT") {
                    if (fc->GetArgs().empty() || fc->GetArgs()[0]->GetType() == ExpressionType::STAR) {
                        atype = AggregateType::COUNT_STAR;
                    } else {
                        atype = AggregateType::COUNT;
                    }
                    rtype = TypeId::BIGINT;
                } else if (fname == "SUM") {
                    atype = AggregateType::SUM;
                    rtype = TypeId::DOUBLE;
                } else if (fname == "AVG") {
                    atype = AggregateType::AVG;
                    rtype = TypeId::DOUBLE;
                } else if (fname == "MIN" || fname == "MAX") {
                    atype = (fname == "MIN") ? AggregateType::MIN : AggregateType::MAX;
                    if (!fc->GetArgs().empty() && fc->GetArgs()[0]->GetType() == ExpressionType::COLUMN_REF) {
                        auto* cr = dynamic_cast<const ColumnRefExpression*>(fc->GetArgs()[0].get());
                        std::string col_lookup = cr->GetColumnName();
                        if (!cr->GetTableName().empty()) {
                            col_lookup = cr->GetTableName() + "." + cr->GetColumnName();
                        }
                        uint32_t cidx = current_head->GetOutputSchema().GetColIdx(col_lookup);
                        rtype = current_head->GetOutputSchema().GetColumn(cidx).GetType();
                    }
                }

                const Expression* arg = fc->GetArgs().empty() ? nullptr : fc->GetArgs()[0].get();
                agg_defs.push_back({atype, arg, fc->ToString()});
                agg_out_cols.emplace_back(fc->ToString(), rtype);
            }
        }

        Schema agg_schema(std::move(agg_out_cols));
        agg_exec = std::make_unique<AggregateExecutor>(current_head, std::move(group_by_exprs), std::move(agg_defs), agg_schema);
        current_head = agg_exec.get();
    }

    // Sort
    if (!stmt->GetOrderBy().empty()) {
        std::vector<OrderByDef> order_by_copy;
        for (const auto& ob : stmt->GetOrderBy()) {
            if (ob.expr->GetType() == ExpressionType::COLUMN_REF) {
                const auto* cr = dynamic_cast<const ColumnRefExpression*>(ob.expr.get());
                order_by_copy.push_back({std::make_unique<ColumnRefExpression>(cr->GetColumnName(), cr->GetTableName()), ob.is_desc});
            } else {
                order_by_copy.push_back({std::make_unique<ColumnRefExpression>(ob.expr->ToString()), ob.is_desc});
            }
        }
        sort_exec = std::make_unique<SortExecutor>(current_head, std::move(order_by_copy));
        current_head = sort_exec.get();
    }

    // Limit
    if (stmt->GetLimit().has_value()) {
        limit_exec = std::make_unique<LimitExecutor>(current_head, static_cast<size_t>(stmt->GetLimit().value()));
        current_head = limit_exec.get();
    }

    // Projection (if non-aggregation query has specific column projections or expressions)
    bool is_star = (stmt->GetSelectList().size() == 1 &&
                    stmt->GetSelectList()[0]->GetType() == ExpressionType::STAR);

    if (!has_aggregation && !is_star) {
        std::vector<Column> proj_cols;
        std::vector<const Expression*> proj_exprs;

        for (const auto& expr : stmt->GetSelectList()) {
            proj_exprs.push_back(expr.get());
            if (expr->GetType() == ExpressionType::COLUMN_REF) {
                const auto* col_ref = dynamic_cast<const ColumnRefExpression*>(expr.get());
                std::string col_lookup = col_ref->GetColumnName();
                if (!col_ref->GetTableName().empty()) {
                    col_lookup = col_ref->GetTableName() + "." + col_ref->GetColumnName();
                }
                uint32_t col_idx = current_head->GetOutputSchema().GetColIdx(col_lookup);
                proj_cols.push_back(current_head->GetOutputSchema().GetColumn(col_idx));
            } else {
                proj_cols.emplace_back(expr->ToString(), TypeId::VARCHAR, 255);
            }
        }

        Schema proj_schema(std::move(proj_cols));
        proj_exec = std::make_unique<ProjectionExecutor>(current_head, std::move(proj_exprs), std::move(proj_schema));
        current_head = proj_exec.get();
    }

    // Execute pipeline
    current_head->Init();

    std::vector<Record> results;
    Record rec;
    RID rid;
    while (current_head->Next(&rec, &rid)) {
        results.push_back(std::move(rec));
    }

    return QueryResult{true, "", current_head->GetOutputSchema(), std::move(results), 0, 0.0};
}

QueryResult ExecutionEngine::ExecuteUpdate(const UpdateStatement* stmt) {
    Table* table = catalog_->GetTable(stmt->GetTableName());
    if (!table) {
        return QueryResult{false, "Table not found: " + stmt->GetTableName(), {}, {}, 0, 0.0};
    }

    const Schema& schema = table->GetSchema();

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

    for (auto& w : col_widths) {
        if (w < 3) w = 3;
    }

    std::ostringstream ss;

    ss << "+";
    for (size_t w : col_widths) {
        ss << std::string(w + 2, '-') << "+";
    }
    ss << "\n";

    ss << "|";
    for (size_t i = 0; i < headers.size(); ++i) {
        ss << " " << std::left << std::setw(static_cast<int>(col_widths[i])) << headers[i] << " |";
    }
    ss << "\n";

    ss << "+";
    for (size_t w : col_widths) {
        ss << std::string(w + 2, '-') << "+";
    }
    ss << "\n";

    for (const auto& row : grid) {
        ss << "|";
        for (size_t i = 0; i < row.size(); ++i) {
            ss << " " << std::left << std::setw(static_cast<int>(col_widths[i])) << row[i] << " |";
        }
        ss << "\n";
    }

    ss << "+";
    for (size_t w : col_widths) {
        ss << std::string(w + 2, '-') << "+";
    }
    ss << "\n";

    ss << rows.size() << " row(s) in set.\n";
    return ss.str();
}

} // namespace forgedb
