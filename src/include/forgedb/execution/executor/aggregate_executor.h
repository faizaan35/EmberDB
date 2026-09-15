#pragma once

#include "forgedb/execution/executor/abstract_executor.h"
#include "forgedb/sql/ast/ast.h"
#include <vector>
#include <map>

namespace forgedb {

enum class AggregateType {
    COUNT_STAR,
    COUNT,
    SUM,
    AVG,
    MIN,
    MAX
};

struct AggregateDef {
    AggregateType type;
    const Expression* arg_expr{nullptr};
    std::string output_name;
};

struct AggregateState {
    int64_t count{0};
    double sum{0.0};
    bool has_sum{false};
    Value min_val;
    Value max_val;
};

class AggregateExecutor : public AbstractExecutor {
public:
    AggregateExecutor(AbstractExecutor* child,
                      std::vector<const Expression*> group_by_exprs,
                      std::vector<AggregateDef> aggregates,
                      Schema output_schema);

    void Init() override;
    bool Next(Record* record, RID* rid) override;
    const Schema& GetOutputSchema() const override { return output_schema_; }

private:
    AbstractExecutor* child_;
    std::vector<const Expression*> group_by_exprs_;
    std::vector<AggregateDef> aggregates_;
    Schema output_schema_;

    std::vector<Record> output_records_;
    size_t current_idx_{0};
};

} // namespace forgedb
