#include "forgedb/execution/expressions/expression_evaluator.h"
#include <stdexcept>

namespace forgedb {

Value ExpressionEvaluator::Evaluate(const Expression* expr, const Record* record, const Schema* schema) {
    if (!expr) {
        return Value(true); // Empty condition is true
    }

    switch (expr->GetType()) {
        case ExpressionType::LITERAL: {
            const auto* lit = dynamic_cast<const LiteralExpression*>(expr);
            return lit->GetValue();
        }
        case ExpressionType::COLUMN_REF: {
            if (!record || !schema) {
                throw std::runtime_error("Cannot evaluate column reference without record and schema");
            }
            const auto* col_ref = dynamic_cast<const ColumnRefExpression*>(expr);
            const std::string& col_name = col_ref->GetColumnName();
            uint32_t col_idx = schema->GetColIdx(col_name);
            return record->GetValue(*schema, col_idx);
        }
        case ExpressionType::BINARY_OP: {
            const auto* bin = dynamic_cast<const BinaryExpression*>(expr);
            return EvaluateBinary(bin, record, schema);
        }
        case ExpressionType::UNARY_OP: {
            const auto* un = dynamic_cast<const UnaryExpression*>(expr);
            return EvaluateUnary(un, record, schema);
        }
        default:
            throw std::runtime_error("Unsupported expression type in evaluator");
    }
}

bool ExpressionEvaluator::EvaluatePredicate(const Expression* expr, const Record* record, const Schema* schema) {
    if (!expr) {
        return true;
    }
    Value val = Evaluate(expr, record, schema);
    if (val.IsNull() || val.GetTypeId() != TypeId::BOOLEAN) {
        return false;
    }
    return val.GetAsBoolean();
}

Value ExpressionEvaluator::EvaluateBinary(const BinaryExpression* expr, const Record* record, const Schema* schema) {
    Value left = Evaluate(expr->GetLeft(), record, schema);
    Value right = Evaluate(expr->GetRight(), record, schema);

    BinaryOpType op = expr->GetOp();

    // Logical operations
    if (op == BinaryOpType::AND) {
        if (left.IsNull() || right.IsNull()) return Value(false);
        return Value(left.GetAsBoolean() && right.GetAsBoolean());
    }
    if (op == BinaryOpType::OR) {
        bool l = !left.IsNull() && left.GetAsBoolean();
        bool r = !right.IsNull() && right.GetAsBoolean();
        return Value(l || r);
    }

    // Comparison operations
    switch (op) {
        case BinaryOpType::EQUAL:
            return Value(left == right);
        case BinaryOpType::NOT_EQUAL:
            return Value(left != right);
        case BinaryOpType::LESS_THAN:
            return Value(left < right);
        case BinaryOpType::LESS_EQUAL:
            return Value(left <= right);
        case BinaryOpType::GREATER_THAN:
            return Value(left > right);
        case BinaryOpType::GREATER_EQUAL:
            return Value(left >= right);
        default:
            break;
    }

    // Arithmetic operations (+, -, *, /)
    if (left.IsNull() || right.IsNull()) {
        return Value::Null(TypeId::DOUBLE);
    }

    bool is_float = (left.GetTypeId() == TypeId::DOUBLE || right.GetTypeId() == TypeId::DOUBLE);

    if (is_float) {
        double l = left.GetAsDouble();
        double r = right.GetAsDouble();
        switch (op) {
            case BinaryOpType::ADD: return Value(l + r);
            case BinaryOpType::SUB: return Value(l - r);
            case BinaryOpType::MUL: return Value(l * r);
            case BinaryOpType::DIV:
                if (r == 0.0) throw std::runtime_error("Division by zero");
                return Value(l / r);
            default: break;
        }
    } else {
        int64_t l = left.GetAsBigInt();
        int64_t r = right.GetAsBigInt();
        switch (op) {
            case BinaryOpType::ADD: return Value(l + r);
            case BinaryOpType::SUB: return Value(l - r);
            case BinaryOpType::MUL: return Value(l * r);
            case BinaryOpType::DIV:
                if (r == 0) throw std::runtime_error("Division by zero");
                return Value(l / r);
            default: break;
        }
    }

    throw std::runtime_error("Unsupported binary operator");
}

Value ExpressionEvaluator::EvaluateUnary(const UnaryExpression* expr, const Record* record, const Schema* schema) {
    Value val = Evaluate(expr->GetExpr(), record, schema);
    if (val.IsNull()) return val;

    if (expr->GetOp() == UnaryOpType::NOT) {
        return Value(!val.GetAsBoolean());
    } else if (expr->GetOp() == UnaryOpType::NEGATE) {
        if (val.GetTypeId() == TypeId::DOUBLE) {
            return Value(-val.GetAsDouble());
        }
        return Value(-val.GetAsBigInt());
    }

    throw std::runtime_error("Unsupported unary operator");
}

} // namespace forgedb
