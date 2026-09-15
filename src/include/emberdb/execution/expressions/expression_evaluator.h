#pragma once

#include "emberdb/common/types.h"
#include "emberdb/catalog/schema.h"
#include "emberdb/storage/record/record.h"
#include "emberdb/sql/ast/ast.h"

namespace emberdb {

class ExpressionEvaluator {
public:
    static Value Evaluate(const Expression* expr, const Record* record = nullptr, const Schema* schema = nullptr);
    static bool EvaluatePredicate(const Expression* expr, const Record* record = nullptr, const Schema* schema = nullptr);

private:
    static Value EvaluateBinary(const BinaryExpression* expr, const Record* record, const Schema* schema);
    static Value EvaluateUnary(const UnaryExpression* expr, const Record* record, const Schema* schema);
};

} // namespace emberdb
