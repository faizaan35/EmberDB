#include "emberdb/execution/executor/index_scan_executor.h"
#include "emberdb/execution/expressions/expression_evaluator.h"

namespace emberdb {

IndexScanExecutor::IndexScanExecutor(const IndexScanPlanNode* plan)
    : plan_(plan), table_(plan->GetTable()) {}

void IndexScanExecutor::Init() {
    cursor_ = 0;
    rids_.clear();

    if (!plan_ || !plan_->GetIndexInfo()) return;
    auto* btree = plan_->GetIndexInfo()->GetIndex();
    if (!btree) return;

    switch (plan_->GetScanType()) {
        case IndexScanType::POINT_LOOKUP:
            btree->GetValue(plan_->GetLookupKey(), rids_);
            break;
        case IndexScanType::RANGE_SCAN: {
            auto pairs = btree->ScanRange(plan_->GetLowKey(), plan_->GetHighKey());
            rids_.reserve(pairs.size());
            for (const auto& p : pairs) {
                rids_.push_back(p.second);
            }
            break;
        }
        case IndexScanType::FULL_SCAN: {
            auto pairs = btree->ScanAll();
            rids_.reserve(pairs.size());
            for (const auto& p : pairs) {
                rids_.push_back(p.second);
            }
            break;
        }
    }
}

bool IndexScanExecutor::Next(Record* record, RID* rid) {
    if (!table_) return false;

    while (cursor_ < rids_.size()) {
        RID cur_rid = rids_[cursor_++];
        Record r;
        auto s = table_->GetTableHeap()->GetRecord(cur_rid, r);
        if (!s.ok()) continue;

        if (plan_->GetResidualFilter()) {
            Value filter_val = ExpressionEvaluator::Evaluate(plan_->GetResidualFilter(), &r, &table_->GetSchema());
            if (filter_val.IsNull() || !filter_val.GetAsBoolean()) {
                continue;
            }
        }

        *record = std::move(r);
        *rid = cur_rid;
        return true;
    }

    return false;
}

} // namespace emberdb
