#include <catch2/catch.hpp>
#include "forgedb/sql/lexer/lexer.h"

TEST_CASE("Lexer tokenization of SQL primitives", "[sql][lexer]") {
    SECTION("Keywords and case insensitivity") {
        std::string sql = "SELECT select Select From WHERE where";
        forgedb::Lexer lexer(sql);
        auto tokens = lexer.Tokenize();

        REQUIRE(tokens.size() == 7); // 6 keywords + EOF
        REQUIRE(tokens[0].type == forgedb::TokenType::KEYWORD_SELECT);
        REQUIRE(tokens[1].type == forgedb::TokenType::KEYWORD_SELECT);
        REQUIRE(tokens[2].type == forgedb::TokenType::KEYWORD_SELECT);
        REQUIRE(tokens[3].type == forgedb::TokenType::KEYWORD_FROM);
        REQUIRE(tokens[4].type == forgedb::TokenType::KEYWORD_WHERE);
        REQUIRE(tokens[5].type == forgedb::TokenType::KEYWORD_WHERE);
        REQUIRE(tokens[6].type == forgedb::TokenType::END_OF_FILE);
    }

    SECTION("Literals: integer, float, string") {
        std::string sql = "42 3.14159 'Hello, World!' 'It''s escaped'";
        forgedb::Lexer lexer(sql);
        auto tokens = lexer.Tokenize();

        REQUIRE(tokens.size() == 5);
        REQUIRE(tokens[0].type == forgedb::TokenType::INTEGER_LITERAL);
        REQUIRE(tokens[0].lexeme == "42");

        REQUIRE(tokens[1].type == forgedb::TokenType::FLOAT_LITERAL);
        REQUIRE(tokens[1].lexeme == "3.14159");

        REQUIRE(tokens[2].type == forgedb::TokenType::STRING_LITERAL);
        REQUIRE(tokens[2].lexeme == "Hello, World!");

        REQUIRE(tokens[3].type == forgedb::TokenType::STRING_LITERAL);
        REQUIRE(tokens[3].lexeme == "It's escaped");
    }

    SECTION("Operators and punctuation") {
        std::string sql = "= != <> < <= > >= + - * / , ; ( ) .";
        forgedb::Lexer lexer(sql);
        auto tokens = lexer.Tokenize();

        REQUIRE(tokens[0].type == forgedb::TokenType::EQUAL);
        REQUIRE(tokens[1].type == forgedb::TokenType::NOT_EQUAL);
        REQUIRE(tokens[2].type == forgedb::TokenType::NOT_EQUAL);
        REQUIRE(tokens[3].type == forgedb::TokenType::LESS_THAN);
        REQUIRE(tokens[4].type == forgedb::TokenType::LESS_EQUAL);
        REQUIRE(tokens[5].type == forgedb::TokenType::GREATER_THAN);
        REQUIRE(tokens[6].type == forgedb::TokenType::GREATER_EQUAL);
        REQUIRE(tokens[7].type == forgedb::TokenType::PLUS);
        REQUIRE(tokens[8].type == forgedb::TokenType::MINUS);
        REQUIRE(tokens[9].type == forgedb::TokenType::STAR);
        REQUIRE(tokens[10].type == forgedb::TokenType::SLASH);
        REQUIRE(tokens[11].type == forgedb::TokenType::COMMA);
        REQUIRE(tokens[12].type == forgedb::TokenType::SEMICOLON);
        REQUIRE(tokens[13].type == forgedb::TokenType::LEFT_PAREN);
        REQUIRE(tokens[14].type == forgedb::TokenType::RIGHT_PAREN);
        REQUIRE(tokens[15].type == forgedb::TokenType::DOT);
    }

    SECTION("Comments are ignored") {
        std::string sql = "SELECT -- line comment\n name FROM users;";
        forgedb::Lexer lexer(sql);
        auto tokens = lexer.Tokenize();

        REQUIRE(tokens.size() == 6);
        REQUIRE(tokens[0].type == forgedb::TokenType::KEYWORD_SELECT);
        REQUIRE(tokens[1].type == forgedb::TokenType::IDENTIFIER);
        REQUIRE(tokens[1].lexeme == "name");
        REQUIRE(tokens[2].type == forgedb::TokenType::KEYWORD_FROM);
        REQUIRE(tokens[3].type == forgedb::TokenType::IDENTIFIER);
        REQUIRE(tokens[3].lexeme == "users");
        REQUIRE(tokens[4].type == forgedb::TokenType::SEMICOLON);
    }
}
