#pragma once

#include "forgedb/common/status.h"
#include "forgedb/sql/lexer/token.h"
#include "forgedb/sql/ast/ast.h"
#include <vector>
#include <memory>

namespace forgedb {

class Parser {
public:
    explicit Parser(std::vector<Token> tokens);

    Result<std::unique_ptr<Statement>> Parse();

private:
    bool Match(TokenType type);
    bool Check(TokenType type) const;
    const Token& Advance();
    bool IsAtEnd() const;
    const Token& Peek() const;
    const Token& Previous() const;
    Result<const Token*> Consume(TokenType type, const std::string& error_msg);

    Result<std::unique_ptr<Statement>> ParseStatement();
    Result<std::unique_ptr<Statement>> ParseCreateTable();
    Result<std::unique_ptr<Statement>> ParseDropTable();
    Result<std::unique_ptr<Statement>> ParseCreateIndex();
    Result<std::unique_ptr<Statement>> ParseInsert();
    Result<std::unique_ptr<Statement>> ParseSelect();
    Result<std::unique_ptr<Statement>> ParseUpdate();
    Result<std::unique_ptr<Statement>> ParseDelete();
    Result<std::unique_ptr<Statement>> ParseTransaction();

    Result<std::unique_ptr<Expression>> ParseExpression();
    Result<std::unique_ptr<Expression>> ParseOr();
    Result<std::unique_ptr<Expression>> ParseAnd();
    Result<std::unique_ptr<Expression>> ParseNot();
    Result<std::unique_ptr<Expression>> ParseComparison();
    Result<std::unique_ptr<Expression>> ParseTerm();
    Result<std::unique_ptr<Expression>> ParseFactor();
    Result<std::unique_ptr<Expression>> ParseUnary();
    Result<std::unique_ptr<Expression>> ParsePrimary();

    std::vector<Token> tokens_;
    size_t current_{0};
};

} // namespace forgedb
