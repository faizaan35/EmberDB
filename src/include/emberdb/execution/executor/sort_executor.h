#pragma once

#include "emberdb/execution/executor/abstract_executor.h"
#include "emberdb/sql/ast/ast.h"
#include <vector>

namespace emberdb {

class SortExecutor : public AbstractExecutor {
public:
    SortExecutor(AbstractExecutor* child, std::vector<OrderByDef> order_by);

    void Init() override;
    bool Next(Record* record, RID* rid) override;
    const Schema& GetOutputSchema() const override { return child_->GetOutputSchema(); }

private:
    AbstractExecutor* child_;
    std::vector<OrderByDef> order_by_;
    std::vector<Record> sorted_records_;
    size_t current_idx_{0};
};

} // namespace emberdb
