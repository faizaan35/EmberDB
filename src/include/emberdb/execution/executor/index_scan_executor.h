#pragma once

#include "emberdb/execution/executor/abstract_executor.h"
#include "emberdb/planner/plan_node.h"
#include "emberdb/storage/table/table.h"
#include <vector>

namespace emberdb {

/**
 * Volcano iterator executing index scans (point lookups and range scans)
 * via BPlusTreeIndex and resolving tuples from TableHeap.
 */
class IndexScanExecutor : public AbstractExecutor {
public:
    explicit IndexScanExecutor(const IndexScanPlanNode* plan);

    void Init() override;
    bool Next(Record* record, RID* rid) override;
    const Schema& GetOutputSchema() const override { return plan_->GetOutputSchema(); }

private:
    const IndexScanPlanNode* plan_;
    Table* table_;
    std::vector<RID> rids_;
    size_t cursor_{0};
};

} // namespace emberdb
