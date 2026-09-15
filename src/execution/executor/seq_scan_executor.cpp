#include "forgedb/execution/executor/seq_scan_executor.h"
#include "forgedb/execution/expressions/expression_evaluator.h"

namespace forgedb {

SeqScanExecutor::SeqScanExecutor(Table* table, const Expression* filter)
    : table_(table), filter_(filter), iterator_(table->GetTableHeap()->End()) {}

void SeqScanExecutor::Init() {
    iterator_ = table_->GetTableHeap()->Begin();
}

bool SeqScanExecutor::Next(Record* record, RID* rid) {
    while (iterator_ != table_->GetTableHeap()->End()) {
        Record current_rec = *iterator_;
        RID current_rid = iterator_.GetRID();
        ++iterator_;

        if (!filter_ || ExpressionEvaluator::EvaluatePredicate(filter_, &current_rec, &table_->GetSchema())) {
            *record = std::move(current_rec);
            *rid = current_rid;
            return true;
        }
    }
    return false;
}

} // namespace forgedb
