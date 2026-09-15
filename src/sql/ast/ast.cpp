#include "forgedb/sql/ast/ast.h"

namespace forgedb {

std::string BinaryOpToString(BinaryOpType op) {
    switch (op) {
        case BinaryOpType::ADD: return "+";
        case BinaryOpType::SUB: return "-";
        case BinaryOpType::MUL: return "*";
        case BinaryOpType::DIV: return "/";
        case BinaryOpType::EQUAL: return "=";
        case BinaryOpType::NOT_EQUAL: return "!=";
        case BinaryOpType::LESS_THAN: return "<";
        case BinaryOpType::LESS_EQUAL: return "<=";
        case BinaryOpType::GREATER_THAN: return ">";
        case BinaryOpType::GREATER_EQUAL: return ">=";
        case BinaryOpType::AND: return "AND";
        case BinaryOpType::OR: return "OR";
    }
    return "??";
}

std::string CreateTableStatement::ToString() const {
    std::string s = "CREATE TABLE " + table_name_ + " (";
    for (size_t i = 0; i < columns_.size(); ++i) {
        if (i > 0) s += ", ";
        s += columns_[i].name + " " + TypeIdToString(columns_[i].type);
        if (columns_[i].type == TypeId::VARCHAR && columns_[i].length > 0) {
            s += "(" + std::to_string(columns_[i].length) + ")";
        }
        if (!columns_[i].nullable) {
            s += " NOT NULL";
        }
    }
    s += ");";
    return s;
}

std::string InsertStatement::ToString() const {
    std::string s = "INSERT INTO " + table_name_;
    if (!columns_.empty()) {
        s += " (";
        for (size_t i = 0; i < columns_.size(); ++i) {
            if (i > 0) s += ", ";
            s += columns_[i];
        }
        s += ")";
    }
    s += " VALUES ";
    for (size_t i = 0; i < values_.size(); ++i) {
        if (i > 0) s += ", ";
        s += "(";
        for (size_t j = 0; j < values_[i].size(); ++j) {
            if (j > 0) s += ", ";
            s += values_[i][j]->ToString();
        }
        s += ")";
    }
    s += ";";
    return s;
}

std::string SelectStatement::ToString() const {
    std::string s = "SELECT ";
    for (size_t i = 0; i < select_list_.size(); ++i) {
        if (i > 0) s += ", ";
        s += select_list_[i]->ToString();
    }
    s += " FROM " + from_table_;

    for (const auto& j : joins_) {
        std::string jtype = (j.type == JoinType::INNER) ? " INNER JOIN " : " LEFT JOIN ";
        s += jtype + j.table_name;
        if (j.on_condition) {
            s += " ON " + j.on_condition->ToString();
        }
    }

    if (where_clause_) {
        s += " WHERE " + where_clause_->ToString();
    }

    if (!group_by_.empty()) {
        s += " GROUP BY ";
        for (size_t i = 0; i < group_by_.size(); ++i) {
            if (i > 0) s += ", ";
            s += group_by_[i]->ToString();
        }
    }

    if (!order_by_.empty()) {
        s += " ORDER BY ";
        for (size_t i = 0; i < order_by_.size(); ++i) {
            if (i > 0) s += ", ";
            s += order_by_[i].expr->ToString() + (order_by_[i].is_desc ? " DESC" : " ASC");
        }
    }

    if (limit_.has_value()) {
        s += " LIMIT " + std::to_string(limit_.value());
    }

    s += ";";
    return s;
}

std::string UpdateStatement::ToString() const {
    std::string s = "UPDATE " + table_name_ + " SET ";
    for (size_t i = 0; i < assignments_.size(); ++i) {
        if (i > 0) s += ", ";
        s += assignments_[i].first + " = " + assignments_[i].second->ToString();
    }
    if (where_clause_) {
        s += " WHERE " + where_clause_->ToString();
    }
    s += ";";
    return s;
}

std::string DeleteStatement::ToString() const {
    std::string s = "DELETE FROM " + table_name_;
    if (where_clause_) {
        s += " WHERE " + where_clause_->ToString();
    }
    s += ";";
    return s;
}

} // namespace forgedb
