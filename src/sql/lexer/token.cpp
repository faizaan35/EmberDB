#include "emberdb/sql/lexer/token.h"

namespace emberdb {

std::string TokenTypeToString(TokenType type) {
    switch (type) {
        case TokenType::KEYWORD_CREATE: return "CREATE";
        case TokenType::KEYWORD_TABLE: return "TABLE";
        case TokenType::KEYWORD_DROP: return "DROP";
        case TokenType::KEYWORD_INSERT: return "INSERT";
        case TokenType::KEYWORD_INTO: return "INTO";
        case TokenType::KEYWORD_VALUES: return "VALUES";
        case TokenType::KEYWORD_SELECT: return "SELECT";
        case TokenType::KEYWORD_FROM: return "FROM";
        case TokenType::KEYWORD_WHERE: return "WHERE";
        case TokenType::KEYWORD_ORDER: return "ORDER";
        case TokenType::KEYWORD_BY: return "BY";
        case TokenType::KEYWORD_ASC: return "ASC";
        case TokenType::KEYWORD_DESC: return "DESC";
        case TokenType::KEYWORD_LIMIT: return "LIMIT";
        case TokenType::KEYWORD_UPDATE: return "UPDATE";
        case TokenType::KEYWORD_SET: return "SET";
        case TokenType::KEYWORD_DELETE: return "DELETE";
        case TokenType::KEYWORD_JOIN: return "JOIN";
        case TokenType::KEYWORD_INNER: return "INNER";
        case TokenType::KEYWORD_LEFT: return "LEFT";
        case TokenType::KEYWORD_ON: return "ON";
        case TokenType::KEYWORD_GROUP: return "GROUP";
        case TokenType::KEYWORD_INDEX: return "INDEX";
        case TokenType::KEYWORD_BEGIN: return "BEGIN";
        case TokenType::KEYWORD_COMMIT: return "COMMIT";
        case TokenType::KEYWORD_ROLLBACK: return "ROLLBACK";
        case TokenType::KEYWORD_INT: return "INT";
        case TokenType::KEYWORD_BIGINT: return "BIGINT";
        case TokenType::KEYWORD_DOUBLE: return "DOUBLE";
        case TokenType::KEYWORD_BOOLEAN: return "BOOLEAN";
        case TokenType::KEYWORD_VARCHAR: return "VARCHAR";
        case TokenType::KEYWORD_AND: return "AND";
        case TokenType::KEYWORD_OR: return "OR";
        case TokenType::KEYWORD_NOT: return "NOT";
        case TokenType::KEYWORD_TRUE: return "TRUE";
        case TokenType::KEYWORD_FALSE: return "FALSE";
        case TokenType::KEYWORD_NULL: return "NULL";
        case TokenType::KEYWORD_EXPLAIN: return "EXPLAIN";
        case TokenType::EQUAL: return "=";
        case TokenType::NOT_EQUAL: return "!=";
        case TokenType::LESS_THAN: return "<";
        case TokenType::LESS_EQUAL: return "<=";
        case TokenType::GREATER_THAN: return ">";
        case TokenType::GREATER_EQUAL: return ">=";
        case TokenType::PLUS: return "+";
        case TokenType::MINUS: return "-";
        case TokenType::STAR: return "*";
        case TokenType::SLASH: return "/";
        case TokenType::COMMA: return ",";
        case TokenType::SEMICOLON: return ";";
        case TokenType::LEFT_PAREN: return "(";
        case TokenType::RIGHT_PAREN: return ")";
        case TokenType::DOT: return ".";
        case TokenType::IDENTIFIER: return "IDENTIFIER";
        case TokenType::INTEGER_LITERAL: return "INTEGER_LITERAL";
        case TokenType::FLOAT_LITERAL: return "FLOAT_LITERAL";
        case TokenType::STRING_LITERAL: return "STRING_LITERAL";
        case TokenType::END_OF_FILE: return "EOF";
        case TokenType::ILLEGAL: return "ILLEGAL";
    }
    return "UNKNOWN";
}

std::string Token::ToString() const {
    return TokenTypeToString(type) + "(" + lexeme + ")";
}

} // namespace emberdb
