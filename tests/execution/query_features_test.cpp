#include <catch2/catch.hpp>
#include "emberdb/storage/disk/disk_manager.h"
#include "emberdb/catalog/catalog.h"
#include "emberdb/sql/lexer/lexer.h"
#include "emberdb/sql/parser/parser.h"
#include "emberdb/execution/executor/execution_engine.h"
#include <filesystem>
#include <vector>

static emberdb::QueryResult RunQuery(emberdb::ExecutionEngine& engine, const std::string& sql) {
    emberdb::Lexer lexer(sql);
    auto tokens = lexer.Tokenize();
    emberdb::Parser parser(std::move(tokens));
    auto stmt_res = parser.Parse();
    if (!stmt_res.ok()) {
        return emberdb::QueryResult{false, stmt_res.status().ToString(), {}, {}, 0, 0.0};
    }
    return engine.Execute(stmt_res->get());
}

TEST_CASE("Phase 5: ORDER BY and LIMIT", "[execution][query_features]") {
    std::string test_db = "data/test_phase5_order_limit.db";
    if (std::filesystem::exists(test_db)) {
        std::filesystem::remove(test_db);
    }

    emberdb::DiskManager disk_mgr(test_db);
    REQUIRE(disk_mgr.Open().ok());
    emberdb::Catalog catalog(&disk_mgr);
    REQUIRE(catalog.Init().ok());
    emberdb::ExecutionEngine engine(&catalog);

    REQUIRE(RunQuery(engine, "CREATE TABLE items (id INT, score INT);").success);
    REQUIRE(RunQuery(engine, "INSERT INTO items VALUES (1, 50);").success);
    REQUIRE(RunQuery(engine, "INSERT INTO items VALUES (2, 20);").success);
    REQUIRE(RunQuery(engine, "INSERT INTO items VALUES (3, 90);").success);
    REQUIRE(RunQuery(engine, "INSERT INTO items VALUES (4, 10);").success);
    REQUIRE(RunQuery(engine, "INSERT INTO items VALUES (5, 75);").success);

    SECTION("ORDER BY ASC") {
        auto res = RunQuery(engine, "SELECT id, score FROM items ORDER BY score ASC;");
        REQUIRE(res.success);
        REQUIRE(res.rows.size() == 5);
        REQUIRE(res.rows[0].GetValue(res.schema, 1).GetAsInteger() == 10);
        REQUIRE(res.rows[1].GetValue(res.schema, 1).GetAsInteger() == 20);
        REQUIRE(res.rows[2].GetValue(res.schema, 1).GetAsInteger() == 50);
        REQUIRE(res.rows[3].GetValue(res.schema, 1).GetAsInteger() == 75);
        REQUIRE(res.rows[4].GetValue(res.schema, 1).GetAsInteger() == 90);
    }

    SECTION("ORDER BY DESC") {
        auto res = RunQuery(engine, "SELECT id, score FROM items ORDER BY score DESC;");
        REQUIRE(res.success);
        REQUIRE(res.rows.size() == 5);
        REQUIRE(res.rows[0].GetValue(res.schema, 1).GetAsInteger() == 90);
        REQUIRE(res.rows[1].GetValue(res.schema, 1).GetAsInteger() == 75);
        REQUIRE(res.rows[2].GetValue(res.schema, 1).GetAsInteger() == 50);
        REQUIRE(res.rows[3].GetValue(res.schema, 1).GetAsInteger() == 20);
        REQUIRE(res.rows[4].GetValue(res.schema, 1).GetAsInteger() == 10);
    }

    SECTION("LIMIT") {
        auto res = RunQuery(engine, "SELECT * FROM items LIMIT 2;");
        REQUIRE(res.success);
        REQUIRE(res.rows.size() == 2);
    }

    SECTION("ORDER BY + LIMIT") {
        auto res = RunQuery(engine, "SELECT id, score FROM items ORDER BY score DESC LIMIT 3;");
        REQUIRE(res.success);
        REQUIRE(res.rows.size() == 3);
        REQUIRE(res.rows[0].GetValue(res.schema, 1).GetAsInteger() == 90);
        REQUIRE(res.rows[1].GetValue(res.schema, 1).GetAsInteger() == 75);
        REQUIRE(res.rows[2].GetValue(res.schema, 1).GetAsInteger() == 50);
    }
}

TEST_CASE("Phase 5: Aggregations without GROUP BY", "[execution][aggregates]") {
    std::string test_db = "data/test_phase5_aggregates.db";
    if (std::filesystem::exists(test_db)) {
        std::filesystem::remove(test_db);
    }

    emberdb::DiskManager disk_mgr(test_db);
    REQUIRE(disk_mgr.Open().ok());
    emberdb::Catalog catalog(&disk_mgr);
    REQUIRE(catalog.Init().ok());
    emberdb::ExecutionEngine engine(&catalog);

    REQUIRE(RunQuery(engine, "CREATE TABLE stats (val INT);").success);
    REQUIRE(RunQuery(engine, "INSERT INTO stats VALUES (10);").success);
    REQUIRE(RunQuery(engine, "INSERT INTO stats VALUES (20);").success);
    REQUIRE(RunQuery(engine, "INSERT INTO stats VALUES (30);").success);

    SECTION("COUNT(*)") {
        auto res = RunQuery(engine, "SELECT COUNT(*) FROM stats;");
        REQUIRE(res.success);
        REQUIRE(res.rows.size() == 1);
        REQUIRE(res.rows[0].GetValue(res.schema, 0).GetAsBigInt() == 3);
    }

    SECTION("SUM, AVG, MIN, MAX") {
        auto res = RunQuery(engine, "SELECT SUM(val), MIN(val), MAX(val) FROM stats;");
        REQUIRE(res.success);
        REQUIRE(res.rows.size() == 1);
        REQUIRE(res.rows[0].GetValue(res.schema, 0).GetAsDouble() == 60.0);
        REQUIRE(res.rows[0].GetValue(res.schema, 1).GetAsInteger() == 10);
        REQUIRE(res.rows[0].GetValue(res.schema, 2).GetAsInteger() == 30);
    }
}

TEST_CASE("Phase 5 Completion Gate: GROUP BY, ORDER BY, LIMIT", "[execution][gate]") {
    std::string test_db = "data/gate_phase5_features.db";
    if (std::filesystem::exists(test_db)) {
        std::filesystem::remove(test_db);
    }

    emberdb::DiskManager disk_mgr(test_db);
    REQUIRE(disk_mgr.Open().ok());
    emberdb::Catalog catalog(&disk_mgr);
    REQUIRE(catalog.Init().ok());
    emberdb::ExecutionEngine engine(&catalog);

    REQUIRE(RunQuery(engine, "CREATE TABLE users (id INT, name VARCHAR, age INT);").success);

    // Insert sample data
    REQUIRE(RunQuery(engine, "INSERT INTO users VALUES (1, 'Faizaan', 23);").success);
    REQUIRE(RunQuery(engine, "INSERT INTO users VALUES (2, 'Ahmed', 23);").success);
    REQUIRE(RunQuery(engine, "INSERT INTO users VALUES (3, 'Sara', 30);").success);
    REQUIRE(RunQuery(engine, "INSERT INTO users VALUES (4, 'John', 40);").success);
    REQUIRE(RunQuery(engine, "INSERT INTO users VALUES (5, 'Zoe', 40);").success);
    REQUIRE(RunQuery(engine, "INSERT INTO users VALUES (6, 'Alex', 40);").success);
    REQUIRE(RunQuery(engine, "INSERT INTO users VALUES (7, 'Bob', 25);").success);

    // Gate Query:
    // SELECT age, COUNT(*) FROM users GROUP BY age ORDER BY age DESC LIMIT 5;
    auto res = RunQuery(engine, "SELECT age, COUNT(*) FROM users GROUP BY age ORDER BY age DESC LIMIT 5;");
    REQUIRE(res.success);
    REQUIRE(res.rows.size() == 4); // Distinct ages: 40, 30, 25, 23 (4 distinct ages <= limit 5)

    // Age 40 -> 3 users
    REQUIRE(res.rows[0].GetValue(res.schema, 0).GetAsInteger() == 40);
    REQUIRE(res.rows[0].GetValue(res.schema, 1).GetAsBigInt() == 3);

    // Age 30 -> 1 user
    REQUIRE(res.rows[1].GetValue(res.schema, 0).GetAsInteger() == 30);
    REQUIRE(res.rows[1].GetValue(res.schema, 1).GetAsBigInt() == 1);

    // Age 25 -> 1 user
    REQUIRE(res.rows[2].GetValue(res.schema, 0).GetAsInteger() == 25);
    REQUIRE(res.rows[2].GetValue(res.schema, 1).GetAsBigInt() == 1);

    // Age 23 -> 2 users
    REQUIRE(res.rows[3].GetValue(res.schema, 0).GetAsInteger() == 23);
    REQUIRE(res.rows[3].GetValue(res.schema, 1).GetAsBigInt() == 2);
}
