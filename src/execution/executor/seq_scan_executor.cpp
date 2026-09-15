#include "emberdb/execution/executor/seq_scan_executor.h"
#include "emberdb/execution/expressions/expression_evaluator.h"

namespace emberdb {

SeqScanExecutor::SeqScanExecutor(Table* table, const Expression* filter, std::optional<Schema> output_schema)
    : table_(table), filter_(filter), output_schema_(std::move(output_schema)), iterator_(table->GetTableHeap()->End()) {}

void SeqScanExecutor::Init() {
    iterator_ = table_->GetTableHeap()->Begin();
}

bool SeqScanExecutor::Next(Record* record, RID* rid) {
    while (iterator_ != table_->GetTableHeap()->End()) {
        Record current_rec = *iterator_;
        RID current_rid = iterator_.GetRID();
        ++iterator_;

        if (!filter_ || ExpressionEvaluator::EvaluatePredicate(filter_, &current_rec, &GetOutputSchema())) {
            *record = std::move(current_rec);
            *rid = current_rid;
            return true;
        }
    }
    return false;
}

} // namespace emberdb
