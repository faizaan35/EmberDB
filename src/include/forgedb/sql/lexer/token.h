#pragma once

#include <string>

namespace forgedb {

enum class TokenType {
    // Keywords
    KEYWORD_CREATE,
    KEYWORD_TABLE,
    KEYWORD_DROP,
    KEYWORD_INSERT,
    KEYWORD_INTO,
    KEYWORD_VALUES,
    KEYWORD_SELECT,
    KEYWORD_FROM,
    KEYWORD_WHERE,
    KEYWORD_ORDER,
    KEYWORD_BY,
    KEYWORD_ASC,
    KEYWORD_DESC,
    KEYWORD_LIMIT,
    KEYWORD_UPDATE,
    KEYWORD_SET,
    KEYWORD_DELETE,
    KEYWORD_JOIN,
    KEYWORD_INNER,
    KEYWORD_LEFT,
    KEYWORD_ON,
    KEYWORD_GROUP,
    KEYWORD_INDEX,
    KEYWORD_BEGIN,
    KEYWORD_COMMIT,
    KEYWORD_ROLLBACK,
    KEYWORD_INT,
    KEYWORD_BIGINT,
    KEYWORD_DOUBLE,
    KEYWORD_BOOLEAN,
    KEYWORD_VARCHAR,
    KEYWORD_AND,
    KEYWORD_OR,
    KEYWORD_NOT,
    KEYWORD_TRUE,
    KEYWORD_FALSE,
    KEYWORD_NULL,

    // Operators
    EQUAL,          // =
    NOT_EQUAL,      // != or <>
    LESS_THAN,      // <
    LESS_EQUAL,     // <=
    GREATER_THAN,   // >
    GREATER_EQUAL,  // >=
    PLUS,           // +
    MINUS,          // -
    STAR,           // *
    SLASH,          // /

    // Punctuation
    COMMA,          // ,
    SEMICOLON,      // ;
    LEFT_PAREN,     // (
    RIGHT_PAREN,    // )
    DOT,            // .

    // Literals and Identifiers
    IDENTIFIER,
    INTEGER_LITERAL,
    FLOAT_LITERAL,
    STRING_LITERAL,

    // Special
    END_OF_FILE,
    ILLEGAL
};

std::string TokenTypeToString(TokenType type);

struct Token {
    TokenType type{TokenType::ILLEGAL};
    std::string lexeme;
    size_t line{1};
    size_t column{1};

    Token() = default;
    Token(TokenType t, std::string l, size_t ln = 1, size_t col = 1)
        : type(t), lexeme(std::move(l)), line(ln), column(col) {}

    std::string ToString() const;
};

} // namespace forgedb
