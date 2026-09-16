#include "emberdb/sql/lexer/lexer.h"
#include <cctype>
#include <algorithm>

namespace emberdb {

const std::unordered_map<std::string, TokenType> Lexer::keywords_ = {
    {"CREATE", TokenType::KEYWORD_CREATE},
    {"TABLE", TokenType::KEYWORD_TABLE},
    {"DROP", TokenType::KEYWORD_DROP},
    {"INSERT", TokenType::KEYWORD_INSERT},
    {"INTO", TokenType::KEYWORD_INTO},
    {"VALUES", TokenType::KEYWORD_VALUES},
    {"SELECT", TokenType::KEYWORD_SELECT},
    {"FROM", TokenType::KEYWORD_FROM},
    {"WHERE", TokenType::KEYWORD_WHERE},
    {"ORDER", TokenType::KEYWORD_ORDER},
    {"BY", TokenType::KEYWORD_BY},
    {"ASC", TokenType::KEYWORD_ASC},
    {"DESC", TokenType::KEYWORD_DESC},
    {"LIMIT", TokenType::KEYWORD_LIMIT},
    {"UPDATE", TokenType::KEYWORD_UPDATE},
    {"SET", TokenType::KEYWORD_SET},
    {"DELETE", TokenType::KEYWORD_DELETE},
    {"JOIN", TokenType::KEYWORD_JOIN},
    {"INNER", TokenType::KEYWORD_INNER},
    {"LEFT", TokenType::KEYWORD_LEFT},
    {"ON", TokenType::KEYWORD_ON},
    {"GROUP", TokenType::KEYWORD_GROUP},
    {"INDEX", TokenType::KEYWORD_INDEX},
    {"BEGIN", TokenType::KEYWORD_BEGIN},
    {"COMMIT", TokenType::KEYWORD_COMMIT},
    {"ROLLBACK", TokenType::KEYWORD_ROLLBACK},
    {"INT", TokenType::KEYWORD_INT},
    {"INTEGER", TokenType::KEYWORD_INT},
    {"BIGINT", TokenType::KEYWORD_BIGINT},
    {"DOUBLE", TokenType::KEYWORD_DOUBLE},
    {"BOOLEAN", TokenType::KEYWORD_BOOLEAN},
    {"BOOL", TokenType::KEYWORD_BOOLEAN},
    {"VARCHAR", TokenType::KEYWORD_VARCHAR},
    {"AND", TokenType::KEYWORD_AND},
    {"OR", TokenType::KEYWORD_OR},
    {"NOT", TokenType::KEYWORD_NOT},
    {"TRUE", TokenType::KEYWORD_TRUE},
    {"FALSE", TokenType::KEYWORD_FALSE},
    {"NULL", TokenType::KEYWORD_NULL},
    {"EXPLAIN", TokenType::KEYWORD_EXPLAIN}
};

Lexer::Lexer(std::string source) : source_(std::move(source)) {}

std::vector<Token> Lexer::Tokenize() {
    std::vector<Token> tokens;
    while (true) {
        Token tok = NextToken();
        tokens.push_back(tok);
        if (tok.type == TokenType::END_OF_FILE) {
            break;
        }
    }
    return tokens;
}

char Lexer::Peek() const {
    if (IsAtEnd()) return '\0';
    return source_[current_];
}

char Lexer::PeekNext() const {
    if (current_ + 1 >= source_.size()) return '\0';
    return source_[current_ + 1];
}

char Lexer::Advance() {
    char c = source_[current_++];
    ++column_;
    return c;
}

bool Lexer::IsAtEnd() const {
    return current_ >= source_.size();
}

void Lexer::SkipWhitespaceAndComments() {
    while (!IsAtEnd()) {
        char c = Peek();
        if (c == ' ' || c == '\t' || c == '\r') {
            Advance();
        } else if (c == '\n') {
            Advance();
            ++line_;
            column_ = 1;
        } else if (c == '-' && PeekNext() == '-') {
            // SQL line comment
            while (!IsAtEnd() && Peek() != '\n') {
                Advance();
            }
        } else {
            break;
        }
    }
}

Token Lexer::NextToken() {
    SkipWhitespaceAndComments();

    start_ = current_;
    start_col_ = column_;

    if (IsAtEnd()) {
        return Token(TokenType::END_OF_FILE, "", line_, start_col_);
    }

    char c = Advance();

    // Identifiers and keywords
    if (std::isalpha(c) || c == '_') {
        return ScanIdentifierOrKeyword();
    }

    // Numbers (integer or floating-point)
    if (std::isdigit(c)) {
        return ScanNumber();
    }

    // Strings (enclosed in single quotes)
    if (c == '\'') {
        return ScanString();
    }

    // Single or multi-character operators & punctuation
    switch (c) {
        case '=':
            return Token(TokenType::EQUAL, "=", line_, start_col_);
        case '!':
            if (Peek() == '=') {
                Advance();
                return Token(TokenType::NOT_EQUAL, "!=", line_, start_col_);
            }
            return Token(TokenType::ILLEGAL, "!", line_, start_col_);
        case '<':
            if (Peek() == '=') {
                Advance();
                return Token(TokenType::LESS_EQUAL, "<=", line_, start_col_);
            }
            if (Peek() == '>') {
                Advance();
                return Token(TokenType::NOT_EQUAL, "<>", line_, start_col_);
            }
            return Token(TokenType::LESS_THAN, "<", line_, start_col_);
        case '>':
            if (Peek() == '=') {
                Advance();
                return Token(TokenType::GREATER_EQUAL, ">=", line_, start_col_);
            }
            return Token(TokenType::GREATER_THAN, ">", line_, start_col_);
        case '+':
            return Token(TokenType::PLUS, "+", line_, start_col_);
        case '-':
            return Token(TokenType::MINUS, "-", line_, start_col_);
        case '*':
            return Token(TokenType::STAR, "*", line_, start_col_);
        case '/':
            return Token(TokenType::SLASH, "/", line_, start_col_);
        case ',':
            return Token(TokenType::COMMA, ",", line_, start_col_);
        case ';':
            return Token(TokenType::SEMICOLON, ";", line_, start_col_);
        case '(':
            return Token(TokenType::LEFT_PAREN, "(", line_, start_col_);
        case ')':
            return Token(TokenType::RIGHT_PAREN, ")", line_, start_col_);
        case '.':
            return Token(TokenType::DOT, ".", line_, start_col_);
        default:
            return Token(TokenType::ILLEGAL, std::string(1, c), line_, start_col_);
    }
}

Token Lexer::ScanIdentifierOrKeyword() {
    while (!IsAtEnd() && (std::isalnum(Peek()) || Peek() == '_')) {
        Advance();
    }

    std::string lexeme = source_.substr(start_, current_ - start_);

    // Check uppercase keyword
    std::string upper = lexeme;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);

    auto it = keywords_.find(upper);
    if (it != keywords_.end()) {
        return Token(it->second, lexeme, line_, start_col_);
    }

    return Token(TokenType::IDENTIFIER, lexeme, line_, start_col_);
}

Token Lexer::ScanNumber() {
    bool is_float = false;
    while (!IsAtEnd() && std::isdigit(Peek())) {
        Advance();
    }

    if (Peek() == '.' && std::isdigit(PeekNext())) {
        is_float = true;
        Advance(); // consume '.'
        while (!IsAtEnd() && std::isdigit(Peek())) {
            Advance();
        }
    }

    std::string lexeme = source_.substr(start_, current_ - start_);
    return Token(is_float ? TokenType::FLOAT_LITERAL : TokenType::INTEGER_LITERAL,
                 lexeme, line_, start_col_);
}

Token Lexer::ScanString() {
    std::string val;
    while (!IsAtEnd()) {
        char c = Advance();
        if (c == '\'') {
            if (Peek() == '\'') {
                // Escaped single quote ''
                val += '\'';
                Advance();
            } else {
                // End of string
                return Token(TokenType::STRING_LITERAL, val, line_, start_col_);
            }
        } else if (c == '\\' && Peek() == '\'') {
            // Escaped \'
            val += '\'';
            Advance();
        } else {
            val += c;
        }
    }

    return Token(TokenType::ILLEGAL, "Unterminated string literal", line_, start_col_);
}

} // namespace emberdb
