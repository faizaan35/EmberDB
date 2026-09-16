#pragma once

#include "emberdb/catalog/schema.h"
#include "emberdb/storage/table/table.h"
#include "emberdb/index/index_info.h"
#include "emberdb/index/index_key.h"
#include "emberdb/sql/ast/ast.h"
#include <string>
#include <vector>
#include <memory>

namespace emberdb {

enum class PlanNodeType {
    SEQ_SCAN,
    INDEX_SCAN,
    FILTER,
    PROJECTION,
    SORT,
    LIMIT,
    AGGREGATE,
    NESTED_LOOP_JOIN
};

std::string PlanNodeTypeToString(PlanNodeType type);

class AbstractPlanNode {
public:
    AbstractPlanNode(PlanNodeType type, Schema output_schema, std::vector<std::unique_ptr<AbstractPlanNode>> children = {})
        : type_(type), output_schema_(std::move(output_schema)), children_(std::move(children)) {}
    virtual ~AbstractPlanNode() = default;

    PlanNodeType GetType() const { return type_; }
    const Schema& GetOutputSchema() const { return output_schema_; }
    const std::vector<std::unique_ptr<AbstractPlanNode>>& GetChildren() const { return children_; }
    const AbstractPlanNode* GetChildAt(size_t idx) const { return idx < children_.size() ? children_[idx].get() : nullptr; }

    virtual std::string ToString(int indent = 0) const = 0;

protected:
    PlanNodeType type_;
    Schema output_schema_;
    std::vector<std::unique_ptr<AbstractPlanNode>> children_;
};

class SeqScanPlanNode : public AbstractPlanNode {
public:
    SeqScanPlanNode(Schema output_schema, Table* table, std::unique_ptr<Expression> filter = nullptr)
        : AbstractPlanNode(PlanNodeType::SEQ_SCAN, std::move(output_schema)), table_(table), filter_(std::move(filter)) {}

    Table* GetTable() const { return table_; }
    const Expression* GetFilter() const { return filter_.get(); }

    std::string ToString(int indent = 0) const override;

private:
    Table* table_;
    std::unique_ptr<Expression> filter_;
};

enum class IndexScanType {
    POINT_LOOKUP,
    RANGE_SCAN,
    FULL_SCAN
};

class IndexScanPlanNode : public AbstractPlanNode {
public:
    IndexScanPlanNode(Schema output_schema,
                      Table* table,
                      IndexInfo* index_info,
                      IndexScanType scan_type,
                      IndexKey lookup_key = IndexKey{},
                      IndexKey low_key = IndexKey{},
                      IndexKey high_key = IndexKey{},
                      std::unique_ptr<Expression> residual_filter = nullptr)
        : AbstractPlanNode(PlanNodeType::INDEX_SCAN, std::move(output_schema)),
          table_(table),
          index_info_(index_info),
          scan_type_(scan_type),
          lookup_key_(lookup_key),
          low_key_(low_key),
          high_key_(high_key),
          residual_filter_(std::move(residual_filter)) {}

    Table* GetTable() const { return table_; }
    IndexInfo* GetIndexInfo() const { return index_info_; }
    IndexScanType GetScanType() const { return scan_type_; }
    const IndexKey& GetLookupKey() const { return lookup_key_; }
    const IndexKey& GetLowKey() const { return low_key_; }
    const IndexKey& GetHighKey() const { return high_key_; }
    const Expression* GetResidualFilter() const { return residual_filter_.get(); }

    std::string ToString(int indent = 0) const override;

private:
    Table* table_;
    IndexInfo* index_info_;
    IndexScanType scan_type_;
    IndexKey lookup_key_;
    IndexKey low_key_;
    IndexKey high_key_;
    std::unique_ptr<Expression> residual_filter_;
};

class FilterPlanNode : public AbstractPlanNode {
public:
    FilterPlanNode(Schema output_schema, std::unique_ptr<Expression> predicate, std::unique_ptr<AbstractPlanNode> child)
        : AbstractPlanNode(PlanNodeType::FILTER, std::move(output_schema), MakeChildren(std::move(child))),
          predicate_(std::move(predicate)) {}

    const Expression* GetPredicate() const { return predicate_.get(); }
    std::string ToString(int indent = 0) const override;

private:
    static std::vector<std::unique_ptr<AbstractPlanNode>> MakeChildren(std::unique_ptr<AbstractPlanNode> child) {
        std::vector<std::unique_ptr<AbstractPlanNode>> res;
        res.push_back(std::move(child));
        return res;
    }
    std::unique_ptr<Expression> predicate_;
};

class ProjectionPlanNode : public AbstractPlanNode {
public:
    ProjectionPlanNode(Schema output_schema,
                       std::vector<std::unique_ptr<Expression>> expressions,
                       std::unique_ptr<AbstractPlanNode> child)
        : AbstractPlanNode(PlanNodeType::PROJECTION, std::move(output_schema), MakeChildren(std::move(child))),
          expressions_(std::move(expressions)) {}

    const std::vector<std::unique_ptr<Expression>>& GetExpressions() const { return expressions_; }
    std::string ToString(int indent = 0) const override;

private:
    static std::vector<std::unique_ptr<AbstractPlanNode>> MakeChildren(std::unique_ptr<AbstractPlanNode> child) {
        std::vector<std::unique_ptr<AbstractPlanNode>> res;
        res.push_back(std::move(child));
        return res;
    }
    std::vector<std::unique_ptr<Expression>> expressions_;
};

class SortPlanNode : public AbstractPlanNode {
public:
    SortPlanNode(Schema output_schema, std::vector<OrderByDef> order_by, std::unique_ptr<AbstractPlanNode> child)
        : AbstractPlanNode(PlanNodeType::SORT, std::move(output_schema), MakeChildren(std::move(child))),
          order_by_(std::move(order_by)) {}

    const std::vector<OrderByDef>& GetOrderBy() const { return order_by_; }
    std::string ToString(int indent = 0) const override;

private:
    static std::vector<std::unique_ptr<AbstractPlanNode>> MakeChildren(std::unique_ptr<AbstractPlanNode> child) {
        std::vector<std::unique_ptr<AbstractPlanNode>> res;
        res.push_back(std::move(child));
        return res;
    }
    std::vector<OrderByDef> order_by_;
};

class LimitPlanNode : public AbstractPlanNode {
public:
    LimitPlanNode(Schema output_schema, uint32_t limit, std::unique_ptr<AbstractPlanNode> child)
        : AbstractPlanNode(PlanNodeType::LIMIT, std::move(output_schema), MakeChildren(std::move(child))),
          limit_(limit) {}

    uint32_t GetLimit() const { return limit_; }
    std::string ToString(int indent = 0) const override;

private:
    static std::vector<std::unique_ptr<AbstractPlanNode>> MakeChildren(std::unique_ptr<AbstractPlanNode> child) {
        std::vector<std::unique_ptr<AbstractPlanNode>> res;
        res.push_back(std::move(child));
        return res;
    }
    uint32_t limit_;
};

class AggregatePlanNode : public AbstractPlanNode {
public:
    AggregatePlanNode(Schema output_schema,
                      std::vector<std::unique_ptr<Expression>> group_by,
                      std::vector<std::unique_ptr<Expression>> aggregates,
                      std::unique_ptr<AbstractPlanNode> child)
        : AbstractPlanNode(PlanNodeType::AGGREGATE, std::move(output_schema), MakeChildren(std::move(child))),
          group_by_(std::move(group_by)),
          aggregates_(std::move(aggregates)) {}

    const std::vector<std::unique_ptr<Expression>>& GetGroupBy() const { return group_by_; }
    const std::vector<std::unique_ptr<Expression>>& GetAggregates() const { return aggregates_; }
    std::string ToString(int indent = 0) const override;

private:
    static std::vector<std::unique_ptr<AbstractPlanNode>> MakeChildren(std::unique_ptr<AbstractPlanNode> child) {
        std::vector<std::unique_ptr<AbstractPlanNode>> res;
        res.push_back(std::move(child));
        return res;
    }
    std::vector<std::unique_ptr<Expression>> group_by_;
    std::vector<std::unique_ptr<Expression>> aggregates_;
};

class NestedLoopJoinPlanNode : public AbstractPlanNode {
public:
    NestedLoopJoinPlanNode(Schema output_schema,
                           JoinType join_type,
                           std::unique_ptr<Expression> on_condition,
                           std::unique_ptr<AbstractPlanNode> left_child,
                           std::unique_ptr<AbstractPlanNode> right_child)
        : AbstractPlanNode(PlanNodeType::NESTED_LOOP_JOIN, std::move(output_schema), MakeChildren(std::move(left_child), std::move(right_child))),
          join_type_(join_type),
          on_condition_(std::move(on_condition)) {}

    JoinType GetJoinType() const { return join_type_; }
    const Expression* GetOnCondition() const { return on_condition_.get(); }
    std::string ToString(int indent = 0) const override;

private:
    static std::vector<std::unique_ptr<AbstractPlanNode>> MakeChildren(std::unique_ptr<AbstractPlanNode> left,
                                                                       std::unique_ptr<AbstractPlanNode> right) {
        std::vector<std::unique_ptr<AbstractPlanNode>> res;
        res.push_back(std::move(left));
        res.push_back(std::move(right));
        return res;
    }
    JoinType join_type_;
    std::unique_ptr<Expression> on_condition_;
};

} // namespace emberdb
