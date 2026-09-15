#pragma once

#include "forgedb/execution/executor/abstract_executor.h"
#include "forgedb/sql/ast/ast.h"

namespace forgedb {

class FilterExecutor : public AbstractExecutor {
public:
    FilterExecutor(AbstractExecutor* child, const Expression* predicate)
        : child_(child), predicate_(predicate) {}

    void Init() override;
    bool Next(Record* record, RID* rid) override;
    const Schema& GetOutputSchema() const override { return child_->GetOutputSchema(); }

private:
    AbstractExecutor* child_;
    const Expression* predicate_;
};

} // namespace forgedb
