#include "emberdb/execution/executor/sort_executor.h"
#include "emberdb/execution/expressions/expression_evaluator.h"
#include <algorithm>

namespace emberdb {

SortExecutor::SortExecutor(AbstractExecutor* child, std::vector<OrderByDef> order_by)
    : child_(child), order_by_(std::move(order_by)) {}

void SortExecutor::Init() {
    child_->Init();
    sorted_records_.clear();
    current_idx_ = 0;

    Record rec;
    RID rid;
    while (child_->Next(&rec, &rid)) {
        rec.SetRID(rid);
        sorted_records_.push_back(std::move(rec));
    }

    if (order_by_.empty() || sorted_records_.empty()) {
        return;
    }

    const Schema& schema = child_->GetOutputSchema();

    std::stable_sort(sorted_records_.begin(), sorted_records_.end(),
        [this, &schema](const Record& a, const Record& b) {
            for (const auto& ob : order_by_) {
                Value val_a = ExpressionEvaluator::Evaluate(ob.expr.get(), &a, &schema);
                Value val_b = ExpressionEvaluator::Evaluate(ob.expr.get(), &b, &schema);

                if (val_a == val_b) {
                    continue;
                }

                if (ob.is_desc) {
                    return val_b < val_a;
                } else {
                    return val_a < val_b;
                }
            }
            return false;
        });
}

bool SortExecutor::Next(Record* record, RID* rid) {
    if (current_idx_ < sorted_records_.size()) {
        *record = sorted_records_[current_idx_++];
        *rid = record->GetRID();
        return true;
    }
    return false;
}

} // namespace emberdb
