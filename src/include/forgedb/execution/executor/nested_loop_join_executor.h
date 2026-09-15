#pragma once

#include "forgedb/execution/executor/abstract_executor.h"
#include "forgedb/sql/ast/ast.h"

namespace forgedb {

class NestedLoopJoinExecutor : public AbstractExecutor {
public:
    NestedLoopJoinExecutor(AbstractExecutor* left_child,
                           AbstractExecutor* right_child,
                           JoinType join_type,
                           const Expression* on_condition,
                           Schema output_schema)
        : left_child_(left_child),
          right_child_(right_child),
          join_type_(join_type),
          on_condition_(on_condition),
          output_schema_(std::move(output_schema)) {}

    void Init() override;
    bool Next(Record* record, RID* rid) override;
    const Schema& GetOutputSchema() const override { return output_schema_; }

private:
    Record CombineRecords(const Record& left_rec, const Schema& left_schema,
                          const Record& right_rec, const Schema& right_schema) const;
    Record CreateNullRightRecord(const Schema& right_schema) const;

    AbstractExecutor* left_child_;
    AbstractExecutor* right_child_;
    JoinType join_type_;
    const Expression* on_condition_;
    Schema output_schema_;

    Record current_left_record_;
    RID current_left_rid_;
    bool has_left_{false};
    bool left_matched_{false};
};

} // namespace forgedb
