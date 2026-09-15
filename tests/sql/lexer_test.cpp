#include <catch2/catch.hpp>
#include "emberdb/sql/lexer/lexer.h"

TEST_CASE("Lexer tokenization of SQL primitives", "[sql][lexer]") {
    SECTION("Keywords and case insensitivity") {
        std::string sql = "SELECT select Select From WHERE where";
        emberdb::Lexer lexer(sql);
        auto tokens = lexer.Tokenize();

        REQUIRE(tokens.size() == 7); // 6 keywords + EOF
        REQUIRE(tokens[0].type == emberdb::TokenType::KEYWORD_SELECT);
        REQUIRE(tokens[1].type == emberdb::TokenType::KEYWORD_SELECT);
        REQUIRE(tokens[2].type == emberdb::TokenType::KEYWORD_SELECT);
        REQUIRE(tokens[3].type == emberdb::TokenType::KEYWORD_FROM);
        REQUIRE(tokens[4].type == emberdb::TokenType::KEYWORD_WHERE);
        REQUIRE(tokens[5].type == emberdb::TokenType::KEYWORD_WHERE);
        REQUIRE(tokens[6].type == emberdb::TokenType::END_OF_FILE);
    }

    SECTION("Literals: integer, float, string") {
        std::string sql = "42 3.14159 'Hello, World!' 'It''s escaped'";
        emberdb::Lexer lexer(sql);
        auto tokens = lexer.Tokenize();

        REQUIRE(tokens.size() == 5);
        REQUIRE(tokens[0].type == emberdb::TokenType::INTEGER_LITERAL);
        REQUIRE(tokens[0].lexeme == "42");

        REQUIRE(tokens[1].type == emberdb::TokenType::FLOAT_LITERAL);
        REQUIRE(tokens[1].lexeme == "3.14159");

        REQUIRE(tokens[2].type == emberdb::TokenType::STRING_LITERAL);
        REQUIRE(tokens[2].lexeme == "Hello, World!");

        REQUIRE(tokens[3].type == emberdb::TokenType::STRING_LITERAL);
        REQUIRE(tokens[3].lexeme == "It's escaped");
    }

    SECTION("Operators and punctuation") {
        std::string sql = "= != <> < <= > >= + - * / , ; ( ) .";
        emberdb::Lexer lexer(sql);
        auto tokens = lexer.Tokenize();

        REQUIRE(tokens[0].type == emberdb::TokenType::EQUAL);
        REQUIRE(tokens[1].type == emberdb::TokenType::NOT_EQUAL);
        REQUIRE(tokens[2].type == emberdb::TokenType::NOT_EQUAL);
        REQUIRE(tokens[3].type == emberdb::TokenType::LESS_THAN);
        REQUIRE(tokens[4].type == emberdb::TokenType::LESS_EQUAL);
        REQUIRE(tokens[5].type == emberdb::TokenType::GREATER_THAN);
        REQUIRE(tokens[6].type == emberdb::TokenType::GREATER_EQUAL);
        REQUIRE(tokens[7].type == emberdb::TokenType::PLUS);
        REQUIRE(tokens[8].type == emberdb::TokenType::MINUS);
        REQUIRE(tokens[9].type == emberdb::TokenType::STAR);
        REQUIRE(tokens[10].type == emberdb::TokenType::SLASH);
        REQUIRE(tokens[11].type == emberdb::TokenType::COMMA);
        REQUIRE(tokens[12].type == emberdb::TokenType::SEMICOLON);
        REQUIRE(tokens[13].type == emberdb::TokenType::LEFT_PAREN);
        REQUIRE(tokens[14].type == emberdb::TokenType::RIGHT_PAREN);
        REQUIRE(tokens[15].type == emberdb::TokenType::DOT);
    }

    SECTION("Comments are ignored") {
        std::string sql = "SELECT -- line comment\n name FROM users;";
        emberdb::Lexer lexer(sql);
        auto tokens = lexer.Tokenize();

        REQUIRE(tokens.size() == 6);
        REQUIRE(tokens[0].type == emberdb::TokenType::KEYWORD_SELECT);
        REQUIRE(tokens[1].type == emberdb::TokenType::IDENTIFIER);
        REQUIRE(tokens[1].lexeme == "name");
        REQUIRE(tokens[2].type == emberdb::TokenType::KEYWORD_FROM);
        REQUIRE(tokens[3].type == emberdb::TokenType::IDENTIFIER);
        REQUIRE(tokens[3].lexeme == "users");
        REQUIRE(tokens[4].type == emberdb::TokenType::SEMICOLON);
    }
}
