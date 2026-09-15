#include "emberdb/execution/executor/aggregate_executor.h"
#include "emberdb/execution/expressions/expression_evaluator.h"

namespace emberdb {

AggregateExecutor::AggregateExecutor(AbstractExecutor* child,
                                     std::vector<const Expression*> group_by_exprs,
                                     std::vector<AggregateDef> aggregates,
                                     Schema output_schema)
    : child_(child),
      group_by_exprs_(std::move(group_by_exprs)),
      aggregates_(std::move(aggregates)),
      output_schema_(std::move(output_schema)) {}

void AggregateExecutor::Init() {
    child_->Init();
    output_records_.clear();
    current_idx_ = 0;

    const Schema& child_schema = child_->GetOutputSchema();

    // Map group key -> vector of aggregate states
    std::map<std::vector<Value>, std::vector<AggregateState>> groups;

    Record child_rec;
    RID child_rid;

    while (child_->Next(&child_rec, &child_rid)) {

        // Build group key
        std::vector<Value> group_key;
        group_key.reserve(group_by_exprs_.size());
        for (const auto* expr : group_by_exprs_) {
            group_key.push_back(ExpressionEvaluator::Evaluate(expr, &child_rec, &child_schema));
        }

        // Initialize aggregate states if new group
        auto it = groups.find(group_key);
        if (it == groups.end()) {
            groups[group_key] = std::vector<AggregateState>(aggregates_.size());
        }

        auto& states = groups[group_key];

        // Update each aggregate
        for (size_t i = 0; i < aggregates_.size(); ++i) {
            const auto& agg = aggregates_[i];
            auto& state = states[i];

            if (agg.type == AggregateType::COUNT_STAR) {
                state.count++;
            } else {
                Value val = ExpressionEvaluator::Evaluate(agg.arg_expr, &child_rec, &child_schema);
                if (!val.IsNull()) {
                    state.count++;
                    double num_val = val.GetAsDouble();
                    state.sum += num_val;
                    state.has_sum = true;

                    if (state.min_val.IsNull() || val < state.min_val) {
                        state.min_val = val;
                    }
                    if (state.max_val.IsNull() || state.max_val < val) {
                        state.max_val = val;
                    }
                }
            }
        }
    }

    // Special case: scalar aggregation with 0 input rows (e.g. SELECT COUNT(*) FROM empty)
    if (groups.empty() && group_by_exprs_.empty()) {
        std::vector<Value> default_row;
        default_row.reserve(aggregates_.size());
        for (const auto& agg : aggregates_) {
            if (agg.type == AggregateType::COUNT_STAR || agg.type == AggregateType::COUNT) {
                default_row.emplace_back(static_cast<int64_t>(0));
            } else {
                default_row.push_back(Value::Null(TypeId::DOUBLE));
            }
        }
        output_records_.emplace_back(std::move(default_row), output_schema_);
        return;
    }

    // Build output records
    for (const auto& pair : groups) {
        const auto& key = pair.first;
        const auto& states = pair.second;

        std::vector<Value> row_values;
        row_values.reserve(key.size() + states.size());

        // Group key values first
        for (const auto& v : key) {
            row_values.push_back(v);
        }

        // Aggregate values
        for (size_t i = 0; i < aggregates_.size(); ++i) {
            const auto& agg = aggregates_[i];
            const auto& state = states[i];

            switch (agg.type) {
                case AggregateType::COUNT_STAR:
                case AggregateType::COUNT:
                    row_values.emplace_back(state.count);
                    break;
                case AggregateType::SUM:
                    if (state.has_sum) {
                        row_values.emplace_back(state.sum);
                    } else {
                        row_values.push_back(Value::Null(TypeId::DOUBLE));
                    }
                    break;
                case AggregateType::AVG:
                    if (state.count > 0) {
                        row_values.emplace_back(state.sum / static_cast<double>(state.count));
                    } else {
                        row_values.push_back(Value::Null(TypeId::DOUBLE));
                    }
                    break;
                case AggregateType::MIN:
                    row_values.push_back(state.min_val);
                    break;
                case AggregateType::MAX:
                    row_values.push_back(state.max_val);
                    break;
            }
        }

        output_records_.emplace_back(std::move(row_values), output_schema_);
    }
}

bool AggregateExecutor::Next(Record* record, RID* rid) {
    if (current_idx_ < output_records_.size()) {
        *record = output_records_[current_idx_++];
        *rid = RID(0, static_cast<slot_id_t>(current_idx_ - 1));
        return true;
    }
    return false;
}

} // namespace emberdb
