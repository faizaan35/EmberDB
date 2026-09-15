#include "emberdb/execution/executor/projection_executor.h"
#include "emberdb/execution/expressions/expression_evaluator.h"

namespace emberdb {

ProjectionExecutor::ProjectionExecutor(AbstractExecutor* child,
                                       std::vector<const Expression*> expressions,
                                       Schema output_schema)
    : child_(child), expressions_(std::move(expressions)), output_schema_(std::move(output_schema)) {}

void ProjectionExecutor::Init() {
    child_->Init();
}

bool ProjectionExecutor::Next(Record* record, RID* rid) {
    Record child_record;
    if (!child_->Next(&child_record, rid)) {
        return false;
    }

    std::vector<Value> projected_values;
    projected_values.reserve(expressions_.size());

    const Schema& child_schema = child_->GetOutputSchema();
    for (const auto* expr : expressions_) {
        projected_values.push_back(ExpressionEvaluator::Evaluate(expr, &child_record, &child_schema));
    }

    *record = Record(std::move(projected_values), output_schema_);
    record->SetRID(*rid);
    return true;
}

} // namespace emberdb
