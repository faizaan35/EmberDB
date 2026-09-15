#include "forgedb/sql/parser/parser.h"
#include <limits>
#include <algorithm>

namespace forgedb {

Parser::Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

Result<std::unique_ptr<Statement>> Parser::Parse() {
    if (tokens_.empty() || tokens_[0].type == TokenType::END_OF_FILE) {
        return Status::InvalidSyntax("Empty SQL statement");
    }

    auto stmt_res = ParseStatement();
    if (!stmt_res.ok()) {
        return stmt_res;
    }

    // Consume optional semicolon
    Match(TokenType::SEMICOLON);

    return stmt_res;
}

bool Parser::Match(TokenType type) {
    if (Check(type)) {
        Advance();
        return true;
    }
    return false;
}

bool Parser::Check(TokenType type) const {
    if (IsAtEnd()) return false;
    return Peek().type == type;
}

const Token& Parser::Advance() {
    if (!IsAtEnd()) ++current_;
    return Previous();
}

bool Parser::IsAtEnd() const {
    return Peek().type == TokenType::END_OF_FILE;
}

const Token& Parser::Peek() const {
    return tokens_[current_];
}

const Token& Parser::Previous() const {
    return tokens_[current_ - 1];
}

Result<const Token*> Parser::Consume(TokenType type, const std::string& error_msg) {
    if (Check(type)) {
        return &Advance();
    }
    return Status::InvalidSyntax("Syntax error at line " + std::to_string(Peek().line) +
                                 ", col " + std::to_string(Peek().column) +
                                 ": " + error_msg + " (found '" + Peek().lexeme + "')");
}

Result<std::unique_ptr<Statement>> Parser::ParseStatement() {
    if (Match(TokenType::KEYWORD_CREATE)) {
        if (Match(TokenType::KEYWORD_TABLE)) {
            return ParseCreateTable();
        } else if (Match(TokenType::KEYWORD_INDEX)) {
            return ParseCreateIndex();
        }
        return Status::InvalidSyntax("Expected TABLE or INDEX after CREATE");
    } else if (Match(TokenType::KEYWORD_DROP)) {
        if (Match(TokenType::KEYWORD_TABLE)) {
            return ParseDropTable();
        }
        return Status::InvalidSyntax("Expected TABLE after DROP");
    } else if (Match(TokenType::KEYWORD_INSERT)) {
        return ParseInsert();
    } else if (Match(TokenType::KEYWORD_SELECT)) {
        return ParseSelect();
    } else if (Match(TokenType::KEYWORD_UPDATE)) {
        return ParseUpdate();
    } else if (Match(TokenType::KEYWORD_DELETE)) {
        return ParseDelete();
    } else if (Check(TokenType::KEYWORD_BEGIN) || Check(TokenType::KEYWORD_COMMIT) || Check(TokenType::KEYWORD_ROLLBACK)) {
        return ParseTransaction();
    }

    return Status::InvalidSyntax("Unexpected statement beginning with '" + Peek().lexeme + "'");
}

Result<std::unique_ptr<Statement>> Parser::ParseCreateTable() {
    auto name_tok = Consume(TokenType::IDENTIFIER, "Expected table name after CREATE TABLE");
    if (!name_tok.ok()) return name_tok.status();
    std::string table_name = (*name_tok)->lexeme;

    auto lparen = Consume(TokenType::LEFT_PAREN, "Expected '(' after table name");
    if (!lparen.ok()) return lparen.status();

    std::vector<ColumnDef> columns;
    while (!Check(TokenType::RIGHT_PAREN) && !IsAtEnd()) {
        auto col_name_tok = Consume(TokenType::IDENTIFIER, "Expected column name");
        if (!col_name_tok.ok()) return col_name_tok.status();
        std::string col_name = (*col_name_tok)->lexeme;

        TypeId col_type = TypeId::INVALID;
        uint32_t length = 0;

        if (Match(TokenType::KEYWORD_INT)) {
            col_type = TypeId::INTEGER;
        } else if (Match(TokenType::KEYWORD_BIGINT)) {
            col_type = TypeId::BIGINT;
        } else if (Match(TokenType::KEYWORD_DOUBLE)) {
            col_type = TypeId::DOUBLE;
        } else if (Match(TokenType::KEYWORD_BOOLEAN)) {
            col_type = TypeId::BOOLEAN;
        } else if (Match(TokenType::KEYWORD_VARCHAR)) {
            col_type = TypeId::VARCHAR;
            length = 255;
            if (Match(TokenType::LEFT_PAREN)) {
                auto len_tok = Consume(TokenType::INTEGER_LITERAL, "Expected integer length for VARCHAR");
                if (!len_tok.ok()) return len_tok.status();
                length = static_cast<uint32_t>(std::stoul((*len_tok)->lexeme));
                auto rparen_len = Consume(TokenType::RIGHT_PAREN, "Expected ')' after VARCHAR length");
                if (!rparen_len.ok()) return rparen_len.status();
            }
        } else {
            return Status::InvalidSyntax("Expected data type for column '" + col_name + "'");
        }

        bool nullable = true;
        if (Match(TokenType::KEYWORD_NOT)) {
            auto null_tok = Consume(TokenType::KEYWORD_NULL, "Expected NULL after NOT");
            if (!null_tok.ok()) return null_tok.status();
            nullable = false;
        }

        columns.push_back({col_name, col_type, length, nullable});

        if (!Match(TokenType::COMMA)) {
            break;
        }
    }

    auto rparen = Consume(TokenType::RIGHT_PAREN, "Expected ')' at end of column list");
    if (!rparen.ok()) return rparen.status();

    return std::make_unique<CreateTableStatement>(table_name, std::move(columns));
}

Result<std::unique_ptr<Statement>> Parser::ParseDropTable() {
    auto name_tok = Consume(TokenType::IDENTIFIER, "Expected table name after DROP TABLE");
    if (!name_tok.ok()) return name_tok.status();
    return std::make_unique<DropTableStatement>((*name_tok)->lexeme);
}

Result<std::unique_ptr<Statement>> Parser::ParseCreateIndex() {
    auto idx_name_tok = Consume(TokenType::IDENTIFIER, "Expected index name after CREATE INDEX");
    if (!idx_name_tok.ok()) return idx_name_tok.status();

    auto on_tok = Consume(TokenType::KEYWORD_ON, "Expected ON after index name");
    if (!on_tok.ok()) return on_tok.status();

    auto tbl_tok = Consume(TokenType::IDENTIFIER, "Expected table name after ON");
    if (!tbl_tok.ok()) return tbl_tok.status();

    auto lparen = Consume(TokenType::LEFT_PAREN, "Expected '(' before column name");
    if (!lparen.ok()) return lparen.status();

    auto col_tok = Consume(TokenType::IDENTIFIER, "Expected column name for index");
    if (!col_tok.ok()) return col_tok.status();

    auto rparen = Consume(TokenType::RIGHT_PAREN, "Expected ')' after column name");
    if (!rparen.ok()) return rparen.status();

    return std::make_unique<CreateIndexStatement>((*idx_name_tok)->lexeme, (*tbl_tok)->lexeme, (*col_tok)->lexeme);
}

Result<std::unique_ptr<Statement>> Parser::ParseInsert() {
    auto into_tok = Consume(TokenType::KEYWORD_INTO, "Expected INTO after INSERT");
    if (!into_tok.ok()) return into_tok.status();

    auto name_tok = Consume(TokenType::IDENTIFIER, "Expected table name after INSERT INTO");
    if (!name_tok.ok()) return name_tok.status();
    std::string table_name = (*name_tok)->lexeme;

    std::vector<std::string> columns;
    if (Match(TokenType::LEFT_PAREN)) {
        while (!Check(TokenType::RIGHT_PAREN) && !IsAtEnd()) {
            auto col_tok = Consume(TokenType::IDENTIFIER, "Expected column name in column list");
            if (!col_tok.ok()) return col_tok.status();
            columns.push_back((*col_tok)->lexeme);
            if (!Match(TokenType::COMMA)) break;
        }
        auto rparen = Consume(TokenType::RIGHT_PAREN, "Expected ')' after column list");
        if (!rparen.ok()) return rparen.status();
    }

    auto val_tok = Consume(TokenType::KEYWORD_VALUES, "Expected VALUES in INSERT statement");
    if (!val_tok.ok()) return val_tok.status();

    std::vector<std::vector<std::unique_ptr<Expression>>> all_values;
    do {
        auto lparen = Consume(TokenType::LEFT_PAREN, "Expected '(' to begin value list");
        if (!lparen.ok()) return lparen.status();

        std::vector<std::unique_ptr<Expression>> row_values;
        while (!Check(TokenType::RIGHT_PAREN) && !IsAtEnd()) {
            auto expr_res = ParseExpression();
            if (!expr_res.ok()) return expr_res.status();
            row_values.push_back(std::move(*expr_res));
            if (!Match(TokenType::COMMA)) break;
        }

        auto rparen = Consume(TokenType::RIGHT_PAREN, "Expected ')' after value list");
        if (!rparen.ok()) return rparen.status();

        all_values.push_back(std::move(row_values));
    } while (Match(TokenType::COMMA));

    return std::make_unique<InsertStatement>(table_name, std::move(columns), std::move(all_values));
}

Result<std::unique_ptr<Statement>> Parser::ParseSelect() {
    std::vector<std::unique_ptr<Expression>> select_list;

    if (Match(TokenType::STAR)) {
        select_list.push_back(std::make_unique<StarExpression>());
    } else {
        do {
            auto expr_res = ParseExpression();
            if (!expr_res.ok()) return expr_res.status();
            select_list.push_back(std::move(*expr_res));
        } while (Match(TokenType::COMMA));
    }

    auto from_tok = Consume(TokenType::KEYWORD_FROM, "Expected FROM in SELECT query");
    if (!from_tok.ok()) return from_tok.status();

    auto table_tok = Consume(TokenType::IDENTIFIER, "Expected table name after FROM");
    if (!table_tok.ok()) return table_tok.status();
    std::string from_table = (*table_tok)->lexeme;

    // Joins
    std::vector<JoinDef> joins;
    while (Check(TokenType::KEYWORD_INNER) || Check(TokenType::KEYWORD_LEFT) || Check(TokenType::KEYWORD_JOIN)) {
        JoinType jtype = JoinType::INNER;
        if (Match(TokenType::KEYWORD_LEFT)) {
            Match(TokenType::KEYWORD_JOIN);
            jtype = JoinType::LEFT;
        } else {
            Match(TokenType::KEYWORD_INNER);
            auto join_tok = Consume(TokenType::KEYWORD_JOIN, "Expected JOIN");
            if (!join_tok.ok()) return join_tok.status();
        }

        auto join_table_tok = Consume(TokenType::IDENTIFIER, "Expected table name in JOIN");
        if (!join_table_tok.ok()) return join_table_tok.status();

        auto on_tok = Consume(TokenType::KEYWORD_ON, "Expected ON after JOIN table");
        if (!on_tok.ok()) return on_tok.status();

        auto on_cond = ParseExpression();
        if (!on_cond.ok()) return on_cond.status();

        joins.push_back({jtype, (*join_table_tok)->lexeme, std::move(*on_cond)});
    }

    // WHERE clause
    std::unique_ptr<Expression> where_clause;
    if (Match(TokenType::KEYWORD_WHERE)) {
        auto where_res = ParseExpression();
        if (!where_res.ok()) return where_res.status();
        where_clause = std::move(*where_res);
    }

    // GROUP BY clause
    std::vector<std::unique_ptr<Expression>> group_by;
    if (Match(TokenType::KEYWORD_GROUP)) {
        auto by_tok = Consume(TokenType::KEYWORD_BY, "Expected BY after GROUP");
        if (!by_tok.ok()) return by_tok.status();

        do {
            auto expr_res = ParseExpression();
            if (!expr_res.ok()) return expr_res.status();
            group_by.push_back(std::move(*expr_res));
        } while (Match(TokenType::COMMA));
    }

    // ORDER BY clause
    std::vector<OrderByDef> order_by;
    if (Match(TokenType::KEYWORD_ORDER)) {
        auto by_tok = Consume(TokenType::KEYWORD_BY, "Expected BY after ORDER");
        if (!by_tok.ok()) return by_tok.status();

        do {
            auto expr_res = ParseExpression();
            if (!expr_res.ok()) return expr_res.status();
            bool is_desc = false;
            if (Match(TokenType::KEYWORD_DESC)) {
                is_desc = true;
            } else {
                Match(TokenType::KEYWORD_ASC);
            }
            order_by.push_back({std::move(*expr_res), is_desc});
        } while (Match(TokenType::COMMA));
    }

    // LIMIT clause
    std::optional<int32_t> limit;
    if (Match(TokenType::KEYWORD_LIMIT)) {
        auto limit_tok = Consume(TokenType::INTEGER_LITERAL, "Expected integer after LIMIT");
        if (!limit_tok.ok()) return limit_tok.status();
        limit = std::stoi((*limit_tok)->lexeme);
    }

    return std::make_unique<SelectStatement>(
        std::move(select_list), from_table, std::move(joins),
        std::move(where_clause), std::move(group_by), std::move(order_by), limit
    );
}

Result<std::unique_ptr<Statement>> Parser::ParseUpdate() {
    auto name_tok = Consume(TokenType::IDENTIFIER, "Expected table name after UPDATE");
    if (!name_tok.ok()) return name_tok.status();
    std::string table_name = (*name_tok)->lexeme;

    auto set_tok = Consume(TokenType::KEYWORD_SET, "Expected SET after UPDATE table name");
    if (!set_tok.ok()) return set_tok.status();

    std::vector<std::pair<std::string, std::unique_ptr<Expression>>> assignments;
    do {
        auto col_tok = Consume(TokenType::IDENTIFIER, "Expected column name in SET clause");
        if (!col_tok.ok()) return col_tok.status();

        auto eq_tok = Consume(TokenType::EQUAL, "Expected '=' in SET assignment");
        if (!eq_tok.ok()) return eq_tok.status();

        auto val_res = ParseExpression();
        if (!val_res.ok()) return val_res.status();

        assignments.emplace_back((*col_tok)->lexeme, std::move(*val_res));
    } while (Match(TokenType::COMMA));

    std::unique_ptr<Expression> where_clause;
    if (Match(TokenType::KEYWORD_WHERE)) {
        auto where_res = ParseExpression();
        if (!where_res.ok()) return where_res.status();
        where_clause = std::move(*where_res);
    }

    return std::make_unique<UpdateStatement>(table_name, std::move(assignments), std::move(where_clause));
}

Result<std::unique_ptr<Statement>> Parser::ParseDelete() {
    auto from_tok = Consume(TokenType::KEYWORD_FROM, "Expected FROM after DELETE");
    if (!from_tok.ok()) return from_tok.status();

    auto name_tok = Consume(TokenType::IDENTIFIER, "Expected table name after DELETE FROM");
    if (!name_tok.ok()) return name_tok.status();
    std::string table_name = (*name_tok)->lexeme;

    std::unique_ptr<Expression> where_clause;
    if (Match(TokenType::KEYWORD_WHERE)) {
        auto where_res = ParseExpression();
        if (!where_res.ok()) return where_res.status();
        where_clause = std::move(*where_res);
    }

    return std::make_unique<DeleteStatement>(table_name, std::move(where_clause));
}

Result<std::unique_ptr<Statement>> Parser::ParseTransaction() {
    if (Match(TokenType::KEYWORD_BEGIN)) {
        Match(TokenType::KEYWORD_TABLE); // optional TRANSACTION word
        return std::make_unique<TransactionStatement>(TransactionType::BEGIN);
    } else if (Match(TokenType::KEYWORD_COMMIT)) {
        return std::make_unique<TransactionStatement>(TransactionType::COMMIT);
    } else if (Match(TokenType::KEYWORD_ROLLBACK)) {
        return std::make_unique<TransactionStatement>(TransactionType::ROLLBACK);
    }
    return Status::InvalidSyntax("Expected BEGIN, COMMIT, or ROLLBACK");
}

// ---------------------------------------------------------------------------
// Expression Parsing with Operator Precedence
// ---------------------------------------------------------------------------

Result<std::unique_ptr<Expression>> Parser::ParseExpression() {
    return ParseOr();
}

Result<std::unique_ptr<Expression>> Parser::ParseOr() {
    auto left_res = ParseAnd();
    if (!left_res.ok()) return left_res;
    auto left = std::move(*left_res);

    while (Match(TokenType::KEYWORD_OR)) {
        auto right_res = ParseAnd();
        if (!right_res.ok()) return right_res;
        left = std::make_unique<BinaryExpression>(std::move(left), BinaryOpType::OR, std::move(*right_res));
    }
    return left;
}

Result<std::unique_ptr<Expression>> Parser::ParseAnd() {
    auto left_res = ParseNot();
    if (!left_res.ok()) return left_res;
    auto left = std::move(*left_res);

    while (Match(TokenType::KEYWORD_AND)) {
        auto right_res = ParseNot();
        if (!right_res.ok()) return right_res;
        left = std::make_unique<BinaryExpression>(std::move(left), BinaryOpType::AND, std::move(*right_res));
    }
    return left;
}

Result<std::unique_ptr<Expression>> Parser::ParseNot() {
    if (Match(TokenType::KEYWORD_NOT)) {
        auto expr_res = ParseNot();
        if (!expr_res.ok()) return expr_res;
        return std::make_unique<UnaryExpression>(UnaryOpType::NOT, std::move(*expr_res));
    }
    return ParseComparison();
}

Result<std::unique_ptr<Expression>> Parser::ParseComparison() {
    auto left_res = ParseTerm();
    if (!left_res.ok()) return left_res;
    auto left = std::move(*left_res);

    BinaryOpType op;
    bool has_op = true;
    if (Match(TokenType::EQUAL)) {
        op = BinaryOpType::EQUAL;
    } else if (Match(TokenType::NOT_EQUAL)) {
        op = BinaryOpType::NOT_EQUAL;
    } else if (Match(TokenType::LESS_THAN)) {
        op = BinaryOpType::LESS_THAN;
    } else if (Match(TokenType::LESS_EQUAL)) {
        op = BinaryOpType::LESS_EQUAL;
    } else if (Match(TokenType::GREATER_THAN)) {
        op = BinaryOpType::GREATER_THAN;
    } else if (Match(TokenType::GREATER_EQUAL)) {
        op = BinaryOpType::GREATER_EQUAL;
    } else {
        has_op = false;
    }

    if (has_op) {
        auto right_res = ParseTerm();
        if (!right_res.ok()) return right_res;
        return std::make_unique<BinaryExpression>(std::move(left), op, std::move(*right_res));
    }

    return left;
}

Result<std::unique_ptr<Expression>> Parser::ParseTerm() {
    auto left_res = ParseFactor();
    if (!left_res.ok()) return left_res;
    auto left = std::move(*left_res);

    while (Check(TokenType::PLUS) || Check(TokenType::MINUS)) {
        BinaryOpType op = Match(TokenType::PLUS) ? BinaryOpType::ADD : (Match(TokenType::MINUS), BinaryOpType::SUB);
        auto right_res = ParseFactor();
        if (!right_res.ok()) return right_res;
        left = std::make_unique<BinaryExpression>(std::move(left), op, std::move(*right_res));
    }
    return left;
}

Result<std::unique_ptr<Expression>> Parser::ParseFactor() {
    auto left_res = ParseUnary();
    if (!left_res.ok()) return left_res;
    auto left = std::move(*left_res);

    while (Check(TokenType::STAR) || Check(TokenType::SLASH)) {
        BinaryOpType op = Match(TokenType::STAR) ? BinaryOpType::MUL : (Match(TokenType::SLASH), BinaryOpType::DIV);
        auto right_res = ParseUnary();
        if (!right_res.ok()) return right_res;
        left = std::make_unique<BinaryExpression>(std::move(left), op, std::move(*right_res));
    }
    return left;
}

Result<std::unique_ptr<Expression>> Parser::ParseUnary() {
    if (Match(TokenType::MINUS)) {
        auto expr_res = ParseUnary();
        if (!expr_res.ok()) return expr_res;
        return std::make_unique<UnaryExpression>(UnaryOpType::NEGATE, std::move(*expr_res));
    }
    if (Match(TokenType::PLUS)) {
        return ParseUnary();
    }
    return ParsePrimary();
}

Result<std::unique_ptr<Expression>> Parser::ParsePrimary() {
    // Literals
    if (Match(TokenType::INTEGER_LITERAL)) {
        int64_t val = std::stoll(Previous().lexeme);
        if (val >= std::numeric_limits<int32_t>::min() && val <= std::numeric_limits<int32_t>::max()) {
            return std::make_unique<LiteralExpression>(Value(static_cast<int32_t>(val)));
        }
        return std::make_unique<LiteralExpression>(Value(val));
    }
    if (Match(TokenType::FLOAT_LITERAL)) {
        double val = std::stod(Previous().lexeme);
        return std::make_unique<LiteralExpression>(Value(val));
    }
    if (Match(TokenType::STRING_LITERAL)) {
        return std::make_unique<LiteralExpression>(Value(Previous().lexeme));
    }
    if (Match(TokenType::KEYWORD_TRUE)) {
        return std::make_unique<LiteralExpression>(Value(true));
    }
    if (Match(TokenType::KEYWORD_FALSE)) {
        return std::make_unique<LiteralExpression>(Value(false));
    }
    if (Match(TokenType::KEYWORD_NULL)) {
        return std::make_unique<LiteralExpression>(Value::Null(TypeId::VARCHAR));
    }
    if (Match(TokenType::STAR)) {
        return std::make_unique<StarExpression>();
    }

    // Identifiers or Function calls
    if (Match(TokenType::IDENTIFIER)) {
        std::string ident = Previous().lexeme;

        // Function call: IDENT '(' [args] ')'
        if (Match(TokenType::LEFT_PAREN)) {
            std::vector<std::unique_ptr<Expression>> args;
            if (!Check(TokenType::RIGHT_PAREN)) {
                do {
                    auto arg_res = ParseExpression();
                    if (!arg_res.ok()) return arg_res;
                    args.push_back(std::move(*arg_res));
                } while (Match(TokenType::COMMA));
            }
            auto rparen = Consume(TokenType::RIGHT_PAREN, "Expected ')' after function arguments");
            if (!rparen.ok()) return rparen.status();

            std::string upper_ident = ident;
            std::transform(upper_ident.begin(), upper_ident.end(), upper_ident.begin(), ::toupper);
            return std::make_unique<FunctionCallExpression>(upper_ident, std::move(args));
        }

        // Qualified column name: table.column
        if (Match(TokenType::DOT)) {
            auto col_tok = Consume(TokenType::IDENTIFIER, "Expected column name after '.'");
            if (!col_tok.ok()) return col_tok.status();
            return std::make_unique<ColumnRefExpression>((*col_tok)->lexeme, ident);
        }

        return std::make_unique<ColumnRefExpression>(ident);
    }

    // Parentheses
    if (Match(TokenType::LEFT_PAREN)) {
        auto expr_res = ParseExpression();
        if (!expr_res.ok()) return expr_res;
        auto rparen = Consume(TokenType::RIGHT_PAREN, "Expected ')' after expression");
        if (!rparen.ok()) return rparen.status();
        return expr_res;
    }

    return Status::InvalidSyntax("Unexpected token in expression: '" + Peek().lexeme + "'");
}

} // namespace forgedb
