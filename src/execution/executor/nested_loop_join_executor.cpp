#include "emberdb/execution/executor/nested_loop_join_executor.h"
#include "emberdb/execution/expressions/expression_evaluator.h"

namespace emberdb {

void NestedLoopJoinExecutor::Init() {
    left_child_->Init();
    right_child_->Init();
    has_left_ = left_child_->Next(&current_left_record_, &current_left_rid_);
    left_matched_ = false;
}

bool NestedLoopJoinExecutor::Next(Record* record, RID* rid) {
    while (has_left_) {
        Record right_rec;
        RID right_rid;
        while (right_child_->Next(&right_rec, &right_rid)) {
            Record combined = CombineRecords(current_left_record_, left_child_->GetOutputSchema(),
                                             right_rec, right_child_->GetOutputSchema());
            bool match = ExpressionEvaluator::EvaluatePredicate(on_condition_, &combined, &output_schema_);
            if (match) {
                left_matched_ = true;
                *record = std::move(combined);
                *rid = RID();
                return true;
            }
        }

        // Inner loop finished for current left record
        if (join_type_ == JoinType::LEFT && !left_matched_) {
            Record null_right = CreateNullRightRecord(right_child_->GetOutputSchema());
            Record combined = CombineRecords(current_left_record_, left_child_->GetOutputSchema(),
                                             null_right, right_child_->GetOutputSchema());
            has_left_ = left_child_->Next(&current_left_record_, &current_left_rid_);
            left_matched_ = false;
            right_child_->Init();

            *record = std::move(combined);
            *rid = RID();
            return true;
        }

        // Advance left child
        has_left_ = left_child_->Next(&current_left_record_, &current_left_rid_);
        left_matched_ = false;
        if (has_left_) {
            right_child_->Init();
        }
    }

    return false;
}

Record NestedLoopJoinExecutor::CombineRecords(const Record& left_rec, const Schema& left_schema,
                                              const Record& right_rec, const Schema& right_schema) const {
    std::vector<Value> values;
    values.reserve(left_schema.GetColumnCount() + right_schema.GetColumnCount());

    for (uint32_t i = 0; i < left_schema.GetColumnCount(); ++i) {
        values.push_back(left_rec.GetValue(left_schema, i));
    }
    for (uint32_t i = 0; i < right_schema.GetColumnCount(); ++i) {
        values.push_back(right_rec.GetValue(right_schema, i));
    }

    return Record(std::move(values), output_schema_);
}

Record NestedLoopJoinExecutor::CreateNullRightRecord(const Schema& right_schema) const {
    std::vector<Value> values;
    values.reserve(right_schema.GetColumnCount());
    for (uint32_t i = 0; i < right_schema.GetColumnCount(); ++i) {
        values.push_back(Value::Null(right_schema.GetColumn(i).GetType()));
    }
    return Record(std::move(values), right_schema);
}

} // namespace emberdb
