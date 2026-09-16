#pragma once

#include "emberdb/catalog/catalog.h"
#include "emberdb/planner/plan_node.h"
#include "emberdb/sql/ast/ast.h"
#include <memory>

namespace emberdb {

/**
 * Query Planner and Rule-Based Optimizer for EmberDB.
 * Translates AST queries into physical plan trees and selects index scans when appropriate.
 */
class Planner {
public:
    explicit Planner(Catalog* catalog);
    ~Planner() = default;

    std::unique_ptr<AbstractPlanNode> PlanSelect(const SelectStatement* stmt);

    static IndexKey GetMinKeyForType(TypeId type);
    static IndexKey GetMaxKeyForType(TypeId type);

private:
    struct IndexMatchResult {
        bool matched{false};
        IndexInfo* index_info{nullptr};
        IndexScanType scan_type{IndexScanType::POINT_LOOKUP};
        IndexKey lookup_key;
        IndexKey low_key;
        IndexKey high_key;
        std::unique_ptr<Expression> residual_filter;
    };

    IndexMatchResult TryMatchIndex(Table* table, const Expression* where_clause);
    bool ExtractSinglePredicate(const Expression* expr,
                                Table* table,
                                IndexInfo*& matched_idx,
                                IndexScanType& scan_type,
                                IndexKey& lookup_key,
                                IndexKey& low_key,
                                IndexKey& high_key,
                                bool& is_strict);

    Catalog* catalog_;
};

} // namespace emberdb
