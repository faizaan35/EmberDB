#pragma once

#include "emberdb/common/types.h"
#include "emberdb/catalog/column.h"
#include <string>
#include <vector>
#include <memory>
#include <optional>

namespace emberdb {

// ---------------------------------------------------------------------------
// Expressions
// ---------------------------------------------------------------------------

enum class ExpressionType {
    LITERAL,
    COLUMN_REF,
    BINARY_OP,
    UNARY_OP,
    FUNCTION_CALL,
    STAR
};

enum class BinaryOpType {
    ADD, SUB, MUL, DIV,
    EQUAL, NOT_EQUAL, LESS_THAN, LESS_EQUAL, GREATER_THAN, GREATER_EQUAL,
    AND, OR
};

enum class UnaryOpType {
    NOT,
    NEGATE
};

std::string BinaryOpToString(BinaryOpType op);

class Expression {
public:
    virtual ~Expression() = default;
    virtual ExpressionType GetType() const = 0;
    virtual std::string ToString() const = 0;
};

class LiteralExpression : public Expression {
public:
    explicit LiteralExpression(Value val) : value_(std::move(val)) {}
    ExpressionType GetType() const override { return ExpressionType::LITERAL; }
    const Value& GetValue() const { return value_; }
    std::string ToString() const override { return value_.ToString(); }

private:
    Value value_;
};

class ColumnRefExpression : public Expression {
public:
    explicit ColumnRefExpression(std::string col_name, std::string tbl_name = "")
        : col_name_(std::move(col_name)), tbl_name_(std::move(tbl_name)) {}

    ExpressionType GetType() const override { return ExpressionType::COLUMN_REF; }
    const std::string& GetColumnName() const { return col_name_; }
    const std::string& GetTableName() const { return tbl_name_; }

    std::string ToString() const override {
        if (!tbl_name_.empty()) return tbl_name_ + "." + col_name_;
        return col_name_;
    }

private:
    std::string col_name_;
    std::string tbl_name_;
};

class StarExpression : public Expression {
public:
    StarExpression() = default;
    ExpressionType GetType() const override { return ExpressionType::STAR; }
    std::string ToString() const override { return "*"; }
};

class BinaryExpression : public Expression {
public:
    BinaryExpression(std::unique_ptr<Expression> left, BinaryOpType op, std::unique_ptr<Expression> right)
        : left_(std::move(left)), op_(op), right_(std::move(right)) {}

    ExpressionType GetType() const override { return ExpressionType::BINARY_OP; }
    const Expression* GetLeft() const { return left_.get(); }
    BinaryOpType GetOp() const { return op_; }
    const Expression* GetRight() const { return right_.get(); }

    std::string ToString() const override {
        return "(" + left_->ToString() + " " + BinaryOpToString(op_) + " " + right_->ToString() + ")";
    }

private:
    std::unique_ptr<Expression> left_;
    BinaryOpType op_;
    std::unique_ptr<Expression> right_;
};

class UnaryExpression : public Expression {
public:
    UnaryExpression(UnaryOpType op, std::unique_ptr<Expression> expr)
        : op_(op), expr_(std::move(expr)) {}

    ExpressionType GetType() const override { return ExpressionType::UNARY_OP; }
    UnaryOpType GetOp() const { return op_; }
    const Expression* GetExpr() const { return expr_.get(); }

    std::string ToString() const override {
        std::string op_str = (op_ == UnaryOpType::NOT) ? "NOT " : "-";
        return op_str + expr_->ToString();
    }

private:
    UnaryOpType op_;
    std::unique_ptr<Expression> expr_;
};

class FunctionCallExpression : public Expression {
public:
    FunctionCallExpression(std::string func_name, std::vector<std::unique_ptr<Expression>> args)
        : func_name_(std::move(func_name)), args_(std::move(args)) {}

    ExpressionType GetType() const override { return ExpressionType::FUNCTION_CALL; }
    const std::string& GetFunctionName() const { return func_name_; }
    const std::vector<std::unique_ptr<Expression>>& GetArgs() const { return args_; }

    std::string ToString() const override {
        std::string s = func_name_ + "(";
        for (size_t i = 0; i < args_.size(); ++i) {
            if (i > 0) s += ", ";
            s += args_[i]->ToString();
        }
        s += ")";
        return s;
    }

private:
    std::string func_name_;
    std::vector<std::unique_ptr<Expression>> args_;
};

// ---------------------------------------------------------------------------
// Statements
// ---------------------------------------------------------------------------

enum class StatementType {
    CREATE_TABLE,
    DROP_TABLE,
    CREATE_INDEX,
    INSERT,
    SELECT,
    UPDATE,
    DELETE,
    TRANSACTION
};

class Statement {
public:
    virtual ~Statement() = default;
    virtual StatementType GetType() const = 0;
    virtual std::string ToString() const = 0;
};

struct ColumnDef {
    std::string name;
    TypeId type{TypeId::INVALID};
    uint32_t length{0};
    bool nullable{true};
};

class CreateTableStatement : public Statement {
public:
    CreateTableStatement(std::string table_name, std::vector<ColumnDef> columns)
        : table_name_(std::move(table_name)), columns_(std::move(columns)) {}

    StatementType GetType() const override { return StatementType::CREATE_TABLE; }
    const std::string& GetTableName() const { return table_name_; }
    const std::vector<ColumnDef>& GetColumns() const { return columns_; }

    std::string ToString() const override;

private:
    std::string table_name_;
    std::vector<ColumnDef> columns_;
};

class DropTableStatement : public Statement {
public:
    explicit DropTableStatement(std::string table_name) : table_name_(std::move(table_name)) {}
    StatementType GetType() const override { return StatementType::DROP_TABLE; }
    const std::string& GetTableName() const { return table_name_; }
    std::string ToString() const override { return "DROP TABLE " + table_name_ + ";"; }

private:
    std::string table_name_;
};

class CreateIndexStatement : public Statement {
public:
    CreateIndexStatement(std::string index_name, std::string table_name, std::string column_name)
        : index_name_(std::move(index_name)), table_name_(std::move(table_name)), column_name_(std::move(column_name)) {}

    StatementType GetType() const override { return StatementType::CREATE_INDEX; }
    const std::string& GetIndexName() const { return index_name_; }
    const std::string& GetTableName() const { return table_name_; }
    const std::string& GetColumnName() const { return column_name_; }

    std::string ToString() const override {
        return "CREATE INDEX " + index_name_ + " ON " + table_name_ + "(" + column_name_ + ");";
    }

private:
    std::string index_name_;
    std::string table_name_;
    std::string column_name_;
};

class InsertStatement : public Statement {
public:
    InsertStatement(std::string table_name,
                    std::vector<std::string> columns,
                    std::vector<std::vector<std::unique_ptr<Expression>>> values)
        : table_name_(std::move(table_name)), columns_(std::move(columns)), values_(std::move(values)) {}

    StatementType GetType() const override { return StatementType::INSERT; }
    const std::string& GetTableName() const { return table_name_; }
    const std::vector<std::string>& GetColumns() const { return columns_; }
    const std::vector<std::vector<std::unique_ptr<Expression>>>& GetValues() const { return values_; }

    std::string ToString() const override;

private:
    std::string table_name_;
    std::vector<std::string> columns_;
    std::vector<std::vector<std::unique_ptr<Expression>>> values_;
};

enum class JoinType {
    INNER,
    LEFT
};

struct JoinDef {
    JoinType type{JoinType::INNER};
    std::string table_name;
    std::unique_ptr<Expression> on_condition;
};

struct OrderByDef {
    std::unique_ptr<Expression> expr;
    bool is_desc{false};
};

class SelectStatement : public Statement {
public:
    SelectStatement(std::vector<std::unique_ptr<Expression>> select_list,
                    std::string from_table,
                    std::vector<JoinDef> joins,
                    std::unique_ptr<Expression> where_clause,
                    std::vector<std::unique_ptr<Expression>> group_by,
                    std::vector<OrderByDef> order_by,
                    std::optional<int32_t> limit)
        : select_list_(std::move(select_list)),
          from_table_(std::move(from_table)),
          joins_(std::move(joins)),
          where_clause_(std::move(where_clause)),
          group_by_(std::move(group_by)),
          order_by_(std::move(order_by)),
          limit_(limit) {}

    StatementType GetType() const override { return StatementType::SELECT; }
    const std::vector<std::unique_ptr<Expression>>& GetSelectList() const { return select_list_; }
    const std::string& GetFromTable() const { return from_table_; }
    const std::vector<JoinDef>& GetJoins() const { return joins_; }
    const Expression* GetWhereClause() const { return where_clause_.get(); }
    const std::vector<std::unique_ptr<Expression>>& GetGroupBy() const { return group_by_; }
    const std::vector<OrderByDef>& GetOrderBy() const { return order_by_; }
    std::optional<int32_t> GetLimit() const { return limit_; }

    std::string ToString() const override;

private:
    std::vector<std::unique_ptr<Expression>> select_list_;
    std::string from_table_;
    std::vector<JoinDef> joins_;
    std::unique_ptr<Expression> where_clause_;
    std::vector<std::unique_ptr<Expression>> group_by_;
    std::vector<OrderByDef> order_by_;
    std::optional<int32_t> limit_;
};

class UpdateStatement : public Statement {
public:
    UpdateStatement(std::string table_name,
                    std::vector<std::pair<std::string, std::unique_ptr<Expression>>> assignments,
                    std::unique_ptr<Expression> where_clause)
        : table_name_(std::move(table_name)),
          assignments_(std::move(assignments)),
          where_clause_(std::move(where_clause)) {}

    StatementType GetType() const override { return StatementType::UPDATE; }
    const std::string& GetTableName() const { return table_name_; }
    const std::vector<std::pair<std::string, std::unique_ptr<Expression>>>& GetAssignments() const { return assignments_; }
    const Expression* GetWhereClause() const { return where_clause_.get(); }

    std::string ToString() const override;

private:
    std::string table_name_;
    std::vector<std::pair<std::string, std::unique_ptr<Expression>>> assignments_;
    std::unique_ptr<Expression> where_clause_;
};

class DeleteStatement : public Statement {
public:
    DeleteStatement(std::string table_name, std::unique_ptr<Expression> where_clause)
        : table_name_(std::move(table_name)), where_clause_(std::move(where_clause)) {}

    StatementType GetType() const override { return StatementType::DELETE; }
    const std::string& GetTableName() const { return table_name_; }
    const Expression* GetWhereClause() const { return where_clause_.get(); }

    std::string ToString() const override;

private:
    std::string table_name_;
    std::unique_ptr<Expression> where_clause_;
};

enum class TransactionType {
    BEGIN,
    COMMIT,
    ROLLBACK
};

class TransactionStatement : public Statement {
public:
    explicit TransactionStatement(TransactionType type) : type_(type) {}
    StatementType GetType() const override { return StatementType::TRANSACTION; }
    TransactionType GetTransactionType() const { return type_; }

    std::string ToString() const override {
        switch (type_) {
            case TransactionType::BEGIN: return "BEGIN;";
            case TransactionType::COMMIT: return "COMMIT;";
            case TransactionType::ROLLBACK: return "ROLLBACK;";
        }
        return "TRANSACTION;";
    }

private:
    TransactionType type_;
};

} // namespace emberdb
