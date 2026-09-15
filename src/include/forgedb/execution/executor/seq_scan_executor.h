#pragma once

#include "forgedb/execution/executor/abstract_executor.h"
#include "forgedb/storage/table/table.h"
#include "forgedb/sql/ast/ast.h"

namespace forgedb {

class SeqScanExecutor : public AbstractExecutor {
public:
    SeqScanExecutor(Table* table, const Expression* filter);

    void Init() override;
    bool Next(Record* record, RID* rid) override;
    const Schema& GetOutputSchema() const override { return table_->GetSchema(); }

private:
    Table* table_;
    const Expression* filter_;
    TableIterator iterator_;
};

} // namespace forgedb
