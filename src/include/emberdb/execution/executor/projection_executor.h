#pragma once

#include "emberdb/execution/executor/abstract_executor.h"
#include "emberdb/sql/ast/ast.h"
#include <vector>

namespace emberdb {

class ProjectionExecutor : public AbstractExecutor {
public:
    ProjectionExecutor(AbstractExecutor* child,
                       std::vector<const Expression*> expressions,
                       Schema output_schema);

    void Init() override;
    bool Next(Record* record, RID* rid) override;
    const Schema& GetOutputSchema() const override { return output_schema_; }

private:
    AbstractExecutor* child_;
    std::vector<const Expression*> expressions_;
    Schema output_schema_;
};

} // namespace emberdb
