#include "emberdb/execution/executor/filter_executor.h"
#include "emberdb/execution/expressions/expression_evaluator.h"

namespace emberdb {

void FilterExecutor::Init() {
    if (child_) {
        child_->Init();
    }
}

bool FilterExecutor::Next(Record* record, RID* rid) {
    if (!child_) return false;

    while (child_->Next(record, rid)) {
        if (!predicate_ || ExpressionEvaluator::EvaluatePredicate(predicate_, record, &child_->GetOutputSchema())) {
            return true;
        }
    }
    return false;
}

} // namespace emberdb
