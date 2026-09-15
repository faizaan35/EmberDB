#pragma once

#include "forgedb/common/types.h"
#include "forgedb/catalog/schema.h"
#include "forgedb/storage/record/record.h"
#include "forgedb/sql/ast/ast.h"

namespace forgedb {

class ExpressionEvaluator {
public:
    static Value Evaluate(const Expression* expr, const Record* record = nullptr, const Schema* schema = nullptr);
    static bool EvaluatePredicate(const Expression* expr, const Record* record = nullptr, const Schema* schema = nullptr);

private:
    static Value EvaluateBinary(const BinaryExpression* expr, const Record* record, const Schema* schema);
    static Value EvaluateUnary(const UnaryExpression* expr, const Record* record, const Schema* schema);
};

} // namespace forgedb
