#pragma once

#include "forgedb/execution/executor/abstract_executor.h"

namespace forgedb {

class LimitExecutor : public AbstractExecutor {
public:
    LimitExecutor(AbstractExecutor* child, size_t limit);

    void Init() override;
    bool Next(Record* record, RID* rid) override;
    const Schema& GetOutputSchema() const override { return child_->GetOutputSchema(); }

private:
    AbstractExecutor* child_;
    size_t limit_;
    size_t count_{0};
};

} // namespace forgedb
