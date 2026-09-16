#include "emberdb/planner/planner.h"
#include <limits>

namespace emberdb {

Planner::Planner(Catalog* catalog) : catalog_(catalog) {}

bool Planner::ExtractSinglePredicate(const Expression* expr,
                                     Table* table,
                                     IndexInfo*& matched_idx,
                                     IndexScanType& scan_type,
                                     IndexKey& lookup_key,
                                     IndexKey& low_key,
                                     IndexKey& high_key) {
    if (!expr || expr->GetType() != ExpressionType::BINARY_OP) return false;

    const auto* bin_expr = dynamic_cast<const BinaryExpression*>(expr);
    const Expression* left = bin_expr->GetLeft();
    const Expression* right = bin_expr->GetRight();
    BinaryOpType op = bin_expr->GetOp();

    std::string col_name;
    Value literal_val;
    bool col_on_left = false;

    if (left->GetType() == ExpressionType::COLUMN_REF && right->GetType() == ExpressionType::LITERAL) {
        col_name = dynamic_cast<const ColumnRefExpression*>(left)->GetColumnName();
        literal_val = dynamic_cast<const LiteralExpression*>(right)->GetValue();
        col_on_left = true;
    } else if (left->GetType() == ExpressionType::LITERAL && right->GetType() == ExpressionType::COLUMN_REF) {
        col_name = dynamic_cast<const ColumnRefExpression*>(right)->GetColumnName();
        literal_val = dynamic_cast<const LiteralExpression*>(left)->GetValue();
        col_on_left = false;
    } else {
        return false;
    }

    // Check if table has an index on this column
    auto table_indexes = catalog_->GetTableIndexes(table->GetName());
    IndexInfo* target_idx = nullptr;
    for (auto* idx : table_indexes) {
        if (idx->GetColumnName() == col_name) {
            target_idx = idx;
            break;
        }
    }
    if (!target_idx) return false;

    matched_idx = target_idx;

    if (op == BinaryOpType::EQUAL) {
        scan_type = IndexScanType::POINT_LOOKUP;
        lookup_key = IndexKey(literal_val);
        return true;
    }

    if (op == BinaryOpType::GREATER_EQUAL || (op == BinaryOpType::GREATER_THAN && col_on_left) ||
        (op == BinaryOpType::LESS_EQUAL && !col_on_left)) {
        scan_type = IndexScanType::RANGE_SCAN;
        low_key = IndexKey(literal_val);
        high_key = IndexKey(std::numeric_limits<int32_t>::max()); // Bounded in range scan
        return true;
    }

    if (op == BinaryOpType::LESS_EQUAL || (op == BinaryOpType::LESS_THAN && col_on_left) ||
        (op == BinaryOpType::GREATER_EQUAL && !col_on_left)) {
        scan_type = IndexScanType::RANGE_SCAN;
        low_key = IndexKey(std::numeric_limits<int32_t>::min());
        high_key = IndexKey(literal_val);
        return true;
    }

    return false;
}

Planner::IndexMatchResult Planner::TryMatchIndex(Table* table, const Expression* where_clause) {
    IndexMatchResult res;
    if (!where_clause) return res;

    // Direct single predicate
    if (ExtractSinglePredicate(where_clause, table, res.index_info, res.scan_type, res.lookup_key, res.low_key, res.high_key)) {
        res.matched = true;
        res.residual_filter = nullptr;
        return res;
    }

    // Conjunction (AND)
    if (where_clause->GetType() == ExpressionType::BINARY_OP) {
        const auto* bin = dynamic_cast<const BinaryExpression*>(where_clause);
        if (bin->GetOp() == BinaryOpType::AND) {
            IndexInfo* left_idx = nullptr;
            IndexScanType left_scan = IndexScanType::POINT_LOOKUP;
            IndexKey left_lookup, left_low, left_high;

            IndexInfo* right_idx = nullptr;
            IndexScanType right_scan = IndexScanType::POINT_LOOKUP;
            IndexKey right_lookup, right_low, right_high;

            bool left_matches = ExtractSinglePredicate(bin->GetLeft(), table, left_idx, left_scan, left_lookup, left_low, left_high);
            bool right_matches = ExtractSinglePredicate(bin->GetRight(), table, right_idx, right_scan, right_lookup, right_low, right_high);

            // Case: Both left and right form a range scan on the same index (e.g. id >= 10 AND id <= 50)
            if (left_matches && right_matches && left_idx == right_idx &&
                left_scan == IndexScanType::RANGE_SCAN && right_scan == IndexScanType::RANGE_SCAN) {
                res.matched = true;
                res.index_info = left_idx;
                res.scan_type = IndexScanType::RANGE_SCAN;
                res.low_key = (left_low.type_id != TypeId::INVALID && !left_low.is_null && left_low > IndexKey(std::numeric_limits<int32_t>::min()))
                                  ? left_low
                                  : right_low;
                res.high_key = (right_high.type_id != TypeId::INVALID && !right_high.is_null && right_high < IndexKey(std::numeric_limits<int32_t>::max()))
                                   ? right_high
                                   : left_high;
                res.residual_filter = nullptr;
                return res;
            }

            if (left_matches) {
                res.matched = true;
                res.index_info = left_idx;
                res.scan_type = left_scan;
                res.lookup_key = left_lookup;
                res.low_key = left_low;
                res.high_key = left_high;
                res.residual_filter = bin->GetRight()->Clone();
                return res;
            }

            if (right_matches) {
                res.matched = true;
                res.index_info = right_idx;
                res.scan_type = right_scan;
                res.lookup_key = right_lookup;
                res.low_key = right_low;
                res.high_key = right_high;
                res.residual_filter = bin->GetLeft()->Clone();
                return res;
            }
        }
    }

    return res;
}

std::unique_ptr<AbstractPlanNode> Planner::PlanSelect(const SelectStatement* stmt) {
    if (!stmt) return nullptr;
    Table* table = catalog_->GetTable(stmt->GetFromTable());
    if (!table) return nullptr;

    const Schema& base_schema = table->GetSchema();
    std::unique_ptr<AbstractPlanNode> current_plan;

    // 1. Scan and Joins
    if (stmt->GetJoins().empty()) {
        auto index_match = TryMatchIndex(table, stmt->GetWhereClause());
        if (index_match.matched) {
            current_plan = std::make_unique<IndexScanPlanNode>(
                base_schema, table, index_match.index_info, index_match.scan_type,
                index_match.lookup_key, index_match.low_key, index_match.high_key,
                std::move(index_match.residual_filter));
        } else {
            current_plan = std::make_unique<SeqScanPlanNode>(base_schema, table);
            if (stmt->GetWhereClause()) {
                current_plan = std::make_unique<FilterPlanNode>(
                    base_schema, stmt->GetWhereClause()->Clone(), std::move(current_plan));
            }
        }
    } else {
        current_plan = std::make_unique<SeqScanPlanNode>(base_schema, table);
        Schema combined_schema = base_schema;

        for (const auto& join_def : stmt->GetJoins()) {
            Table* join_table = catalog_->GetTable(join_def.table_name);
            if (!join_table) return nullptr;

            auto right_scan = std::make_unique<SeqScanPlanNode>(join_table->GetSchema(), join_table);

            std::vector<Column> joined_cols;
            for (size_t i = 0; i < combined_schema.GetColumnCount(); ++i) {
                joined_cols.push_back(combined_schema.GetColumn(i));
            }
            for (size_t i = 0; i < join_table->GetSchema().GetColumnCount(); ++i) {
                auto col = join_table->GetSchema().GetColumn(i);
                joined_cols.emplace_back(join_table->GetName() + "." + col.GetName(), col.GetType(), col.GetLength(), true);
            }
            combined_schema = Schema(std::move(joined_cols));

            std::unique_ptr<Expression> on_cond = join_def.on_condition ? join_def.on_condition->Clone() : nullptr;
            current_plan = std::make_unique<NestedLoopJoinPlanNode>(
                combined_schema, join_def.type, std::move(on_cond), std::move(current_plan), std::move(right_scan));
        }

        if (stmt->GetWhereClause()) {
            current_plan = std::make_unique<FilterPlanNode>(
                combined_schema, stmt->GetWhereClause()->Clone(), std::move(current_plan));
        }
    }

    // 2. Aggregations & Group By
    bool has_aggs = false;
    for (const auto& expr : stmt->GetSelectList()) {
        if (expr->GetType() == ExpressionType::FUNCTION_CALL) {
            has_aggs = true;
            break;
        }
    }
    if (!stmt->GetGroupBy().empty() || has_aggs) {
        std::vector<std::unique_ptr<Expression>> cloned_gb;
        for (const auto& g : stmt->GetGroupBy()) cloned_gb.push_back(g->Clone());

        std::vector<std::unique_ptr<Expression>> cloned_aggs;
        for (const auto& e : stmt->GetSelectList()) {
            if (e->GetType() == ExpressionType::FUNCTION_CALL) {
                cloned_aggs.push_back(e->Clone());
            }
        }
        current_plan = std::make_unique<AggregatePlanNode>(
            current_plan->GetOutputSchema(), std::move(cloned_gb), std::move(cloned_aggs), std::move(current_plan));
    }

    // 3. Sort (ORDER BY)
    if (!stmt->GetOrderBy().empty()) {
        std::vector<OrderByDef> cloned_order;
        for (const auto& ob : stmt->GetOrderBy()) {
            cloned_order.push_back({ob.expr->Clone(), ob.is_desc});
        }
        current_plan = std::make_unique<SortPlanNode>(
            current_plan->GetOutputSchema(), std::move(cloned_order), std::move(current_plan));
    }

    // 4. Limit
    if (stmt->GetLimit().has_value()) {
        current_plan = std::make_unique<LimitPlanNode>(
            current_plan->GetOutputSchema(), *stmt->GetLimit(), std::move(current_plan));
    }

    // 5. Projection
    bool is_star = false;
    if (stmt->GetSelectList().size() == 1 && stmt->GetSelectList()[0]->GetType() == ExpressionType::STAR) {
        is_star = true;
    }
    if (!is_star) {
        std::vector<std::unique_ptr<Expression>> cloned_proj;
        std::vector<Column> proj_cols;
        for (const auto& e : stmt->GetSelectList()) {
            cloned_proj.push_back(e->Clone());
            proj_cols.emplace_back(e->ToString(), TypeId::VARCHAR);
        }
        current_plan = std::make_unique<ProjectionPlanNode>(
            Schema(std::move(proj_cols)), std::move(cloned_proj), std::move(current_plan));
    }

    return current_plan;
}

} // namespace emberdb
