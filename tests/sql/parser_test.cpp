#include <catch2/catch.hpp>
#include "forgedb/sql/lexer/lexer.h"
#include "forgedb/sql/parser/parser.h"

static forgedb::Result<std::unique_ptr<forgedb::Statement>> ParseSQL(const std::string& sql) {
    forgedb::Lexer lexer(sql);
    auto tokens = lexer.Tokenize();
    forgedb::Parser parser(std::move(tokens));
    return parser.Parse();
}

TEST_CASE("Phase 3 Completion Gate: SQL Parser Statement Coverage", "[sql][parser][gate]") {
    SECTION("CREATE TABLE parsing") {
        std::string sql = "CREATE TABLE users (id INT, name VARCHAR(50), age INT NOT NULL, balance DOUBLE);";
        auto res = ParseSQL(sql);
        REQUIRE(res.ok());
        auto* stmt = dynamic_cast<forgedb::CreateTableStatement*>(res->get());
        REQUIRE(stmt != nullptr);
        REQUIRE(stmt->GetTableName() == "users");
        REQUIRE(stmt->GetColumns().size() == 4);
        REQUIRE(stmt->GetColumns()[0].name == "id");
        REQUIRE(stmt->GetColumns()[0].type == forgedb::TypeId::INTEGER);
        REQUIRE(stmt->GetColumns()[1].name == "name");
        REQUIRE(stmt->GetColumns()[1].type == forgedb::TypeId::VARCHAR);
        REQUIRE(stmt->GetColumns()[1].length == 50);
        REQUIRE(stmt->GetColumns()[2].name == "age");
        REQUIRE_FALSE(stmt->GetColumns()[2].nullable);
        REQUIRE(stmt->GetColumns()[3].name == "balance");
        REQUIRE(stmt->GetColumns()[3].type == forgedb::TypeId::DOUBLE);
    }

    SECTION("INSERT INTO parsing") {
        std::string sql = "INSERT INTO users VALUES (1, 'Faizaan', 23);";
        auto res = ParseSQL(sql);
        REQUIRE(res.ok());
        auto* stmt = dynamic_cast<forgedb::InsertStatement*>(res->get());
        REQUIRE(stmt != nullptr);
        REQUIRE(stmt->GetTableName() == "users");
        REQUIRE(stmt->GetColumns().empty());
        REQUIRE(stmt->GetValues().size() == 1);
        REQUIRE(stmt->GetValues()[0].size() == 3);

        std::string sql_multi = "INSERT INTO users (id, name) VALUES (1, 'A'), (2, 'B');";
        auto res_multi = ParseSQL(sql_multi);
        REQUIRE(res_multi.ok());
        auto* stmt_multi = dynamic_cast<forgedb::InsertStatement*>(res_multi->get());
        REQUIRE(stmt_multi != nullptr);
        REQUIRE(stmt_multi->GetColumns().size() == 2);
        REQUIRE(stmt_multi->GetValues().size() == 2);
    }

    SECTION("SELECT query parsing with projections, WHERE, ORDER BY, LIMIT") {
        std::string sql = "SELECT id, name FROM users WHERE age > 20 ORDER BY age DESC LIMIT 10;";
        auto res = ParseSQL(sql);
        REQUIRE(res.ok());
        auto* stmt = dynamic_cast<forgedb::SelectStatement*>(res->get());
        REQUIRE(stmt != nullptr);
        REQUIRE(stmt->GetFromTable() == "users");
        REQUIRE(stmt->GetSelectList().size() == 2);
        REQUIRE(stmt->GetWhereClause() != nullptr);
        REQUIRE(stmt->GetOrderBy().size() == 1);
        REQUIRE(stmt->GetOrderBy()[0].is_desc == true);
        REQUIRE(stmt->GetLimit().has_value());
        REQUIRE(stmt->GetLimit().value() == 10);
    }

    SECTION("SELECT * wildcard") {
        std::string sql = "SELECT * FROM users;";
        auto res = ParseSQL(sql);
        REQUIRE(res.ok());
        auto* stmt = dynamic_cast<forgedb::SelectStatement*>(res->get());
        REQUIRE(stmt != nullptr);
        REQUIRE(stmt->GetSelectList().size() == 1);
        REQUIRE(stmt->GetSelectList()[0]->GetType() == forgedb::ExpressionType::STAR);
    }

    SECTION("JOIN query parsing (INNER & LEFT)") {
        std::string sql = "SELECT users.name, orders.amount FROM users INNER JOIN orders ON users.id = orders.user_id;";
        auto res = ParseSQL(sql);
        REQUIRE(res.ok());
        auto* stmt = dynamic_cast<forgedb::SelectStatement*>(res->get());
        REQUIRE(stmt != nullptr);
        REQUIRE(stmt->GetJoins().size() == 1);
        REQUIRE(stmt->GetJoins()[0].type == forgedb::JoinType::INNER);
        REQUIRE(stmt->GetJoins()[0].table_name == "orders");
        REQUIRE(stmt->GetJoins()[0].on_condition != nullptr);
    }

    SECTION("UPDATE parsing") {
        std::string sql = "UPDATE users SET age = 24, name = 'Ahmed' WHERE id = 1;";
        auto res = ParseSQL(sql);
        REQUIRE(res.ok());
        auto* stmt = dynamic_cast<forgedb::UpdateStatement*>(res->get());
        REQUIRE(stmt != nullptr);
        REQUIRE(stmt->GetTableName() == "users");
        REQUIRE(stmt->GetAssignments().size() == 2);
        REQUIRE(stmt->GetAssignments()[0].first == "age");
        REQUIRE(stmt->GetAssignments()[1].first == "name");
        REQUIRE(stmt->GetWhereClause() != nullptr);
    }

    SECTION("DELETE parsing") {
        std::string sql = "DELETE FROM users WHERE id = 1;";
        auto res = ParseSQL(sql);
        REQUIRE(res.ok());
        auto* stmt = dynamic_cast<forgedb::DeleteStatement*>(res->get());
        REQUIRE(stmt != nullptr);
        REQUIRE(stmt->GetTableName() == "users");
        REQUIRE(stmt->GetWhereClause() != nullptr);

        std::string sql_all = "DELETE FROM users;";
        auto res_all = ParseSQL(sql_all);
        REQUIRE(res_all.ok());
        auto* stmt_all = dynamic_cast<forgedb::DeleteStatement*>(res_all->get());
        REQUIRE(stmt_all != nullptr);
        REQUIRE(stmt_all->GetWhereClause() == nullptr);
    }

    SECTION("Transactions: BEGIN, COMMIT, ROLLBACK") {
        auto res_b = ParseSQL("BEGIN;");
        REQUIRE(res_b.ok());
        auto* b = dynamic_cast<forgedb::TransactionStatement*>(res_b->get());
        REQUIRE(b->GetTransactionType() == forgedb::TransactionType::BEGIN);

        auto res_c = ParseSQL("COMMIT;");
        REQUIRE(res_c.ok());
        auto* c = dynamic_cast<forgedb::TransactionStatement*>(res_c->get());
        REQUIRE(c->GetTransactionType() == forgedb::TransactionType::COMMIT);

        auto res_r = ParseSQL("ROLLBACK;");
        REQUIRE(res_r.ok());
        auto* r = dynamic_cast<forgedb::TransactionStatement*>(res_r->get());
        REQUIRE(r->GetTransactionType() == forgedb::TransactionType::ROLLBACK);
    }

    SECTION("CREATE INDEX and DROP TABLE") {
        auto res_idx = ParseSQL("CREATE INDEX idx_users_id ON users(id);");
        REQUIRE(res_idx.ok());
        auto* idx = dynamic_cast<forgedb::CreateIndexStatement*>(res_idx->get());
        REQUIRE(idx->GetIndexName() == "idx_users_id");
        REQUIRE(idx->GetTableName() == "users");
        REQUIRE(idx->GetColumnName() == "id");

        auto res_drop = ParseSQL("DROP TABLE users;");
        REQUIRE(res_drop.ok());
        auto* drop = dynamic_cast<forgedb::DropTableStatement*>(res_drop->get());
        REQUIRE(drop->GetTableName() == "users");
    }
}

TEST_CASE("SQL Parser syntax error handling", "[sql][parser][errors]") {
    SECTION("Empty SQL") {
        auto res = ParseSQL("");
        REQUIRE_FALSE(res.ok());
        REQUIRE(res.status().code() == forgedb::StatusCode::InvalidSyntax);
    }

    SECTION("Incomplete CREATE TABLE") {
        auto res = ParseSQL("CREATE TABLE users (id INT, ;");
        REQUIRE_FALSE(res.ok());
        REQUIRE(res.status().code() == forgedb::StatusCode::InvalidSyntax);
    }

    SECTION("Missing FROM in SELECT") {
        auto res = ParseSQL("SELECT id, name;");
        REQUIRE_FALSE(res.ok());
        REQUIRE(res.status().code() == forgedb::StatusCode::InvalidSyntax);
    }

    SECTION("Malformed WHERE clause") {
        auto res = ParseSQL("SELECT * FROM users WHERE;");
        REQUIRE_FALSE(res.ok());
        REQUIRE(res.status().code() == forgedb::StatusCode::InvalidSyntax);
    }

    SECTION("Invalid keyword") {
        auto res = ParseSQL("FOOBAR * FROM users;");
        REQUIRE_FALSE(res.ok());
        REQUIRE(res.status().code() == forgedb::StatusCode::InvalidSyntax);
    }
}
