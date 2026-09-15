#pragma once

#include "emberdb/sql/lexer/token.h"
#include <string>
#include <vector>
#include <unordered_map>

namespace emberdb {

class Lexer {
public:
    explicit Lexer(std::string source);

    std::vector<Token> Tokenize();
    Token NextToken();

private:
    char Peek() const;
    char PeekNext() const;
    char Advance();
    bool IsAtEnd() const;
    void SkipWhitespaceAndComments();

    Token ScanIdentifierOrKeyword();
    Token ScanNumber();
    Token ScanString();

    std::string source_;
    size_t start_{0};
    size_t current_{0};
    size_t line_{1};
    size_t column_{1};
    size_t start_col_{1};

    static const std::unordered_map<std::string, TokenType> keywords_;
};

} // namespace emberdb
