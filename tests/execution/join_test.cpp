#include <catch2/catch.hpp>
#include "emberdb/storage/disk/disk_manager.h"
#include "emberdb/catalog/catalog.h"
#include "emberdb/sql/lexer/lexer.h"
#include "emberdb/sql/parser/parser.h"
#include "emberdb/execution/executor/execution_engine.h"
#include <filesystem>

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

TEST_CASE("Phase 6: Nested Loop Join — Comprehensive Suite", "[execution][join]") {
    std::string test_db = "data/test_phase6_joins.db";
    if (std::filesystem::exists(test_db)) {
        std::filesystem::remove(test_db);
    }

    emberdb::DiskManager disk_mgr(test_db);
    REQUIRE(disk_mgr.Open().ok());
    emberdb::Catalog catalog(&disk_mgr);
    REQUIRE(catalog.Init().ok());
    emberdb::ExecutionEngine engine(&catalog);

    // Setup tables:
    // users: id INT, name VARCHAR, age INT
    // orders: id INT, user_id INT, amount INT
    REQUIRE(RunQuery(engine, "CREATE TABLE users (id INT, name VARCHAR, age INT);").success);
    REQUIRE(RunQuery(engine, "CREATE TABLE orders (id INT, user_id INT, amount INT);").success);

    // Users:
    // 1: Alice, 25
    // 2: Bob, 30
    // 3: Charlie, 35
    // 4: David, 40 (no orders)
    REQUIRE(RunQuery(engine, "INSERT INTO users VALUES (1, 'Alice', 25);").success);
    REQUIRE(RunQuery(engine, "INSERT INTO users VALUES (2, 'Bob', 30);").success);
    REQUIRE(RunQuery(engine, "INSERT INTO users VALUES (3, 'Charlie', 35);").success);
    REQUIRE(RunQuery(engine, "INSERT INTO users VALUES (4, 'David', 40);").success);

    // Orders:
    // 101: user 1, 500
    // 102: user 1, 1500 (multiple matches for Alice)
    // 103: user 2, 2000 (Bob)
    // 104: user 99, 3000 (no matching user)
    REQUIRE(RunQuery(engine, "INSERT INTO orders VALUES (101, 1, 500);").success);
    REQUIRE(RunQuery(engine, "INSERT INTO orders VALUES (102, 1, 1500);").success);
    REQUIRE(RunQuery(engine, "INSERT INTO orders VALUES (103, 2, 2000);").success);
    REQUIRE(RunQuery(engine, "INSERT INTO orders VALUES (104, 99, 3000);").success);

    SECTION("Matching rows and multiple matches (INNER JOIN)") {
        auto res = RunQuery(engine, "SELECT * FROM users INNER JOIN orders ON users.id = orders.user_id;");
        REQUIRE(res.success);
        // Alice matches twice (101, 102), Bob matches once (103). Total = 3 rows
        REQUIRE(res.rows.size() == 3);

        // Schema has 6 columns: users.id, users.name, users.age, orders.id, orders.user_id, orders.amount
        REQUIRE(res.schema.GetColumnCount() == 6);
    }

    SECTION("No matching rows (INNER JOIN with false condition)") {
        auto res = RunQuery(engine, "SELECT * FROM users INNER JOIN orders ON users.id = 999;");
        REQUIRE(res.success);
        REQUIRE(res.rows.empty());
    }

    SECTION("LEFT JOIN unmatched rows") {
        // Users 1 (Alice - 2 orders), 2 (Bob - 1 order), 3 (Charlie - 0 orders), 4 (David - 0 orders)
        // Expected rows = 2 + 1 + 1 + 1 = 5 rows
        auto res = RunQuery(engine, "SELECT users.name, orders.id, orders.amount FROM users LEFT JOIN orders ON users.id = orders.user_id;");
        REQUIRE(res.success);
        REQUIRE(res.rows.size() == 5);

        // Find Charlie and David to verify NULL right side
        bool found_charlie_null = false;
        bool found_david_null = false;

        for (const auto& r : res.rows) {
            std::string name = r.GetValue(res.schema, 0).GetAsVarChar();
            if (name == "Charlie") {
                REQUIRE(r.GetValue(res.schema, 1).IsNull());
                REQUIRE(r.GetValue(res.schema, 2).IsNull());
                found_charlie_null = true;
            } else if (name == "David") {
                REQUIRE(r.GetValue(res.schema, 1).IsNull());
                REQUIRE(r.GetValue(res.schema, 2).IsNull());
                found_david_null = true;
            }
        }

        REQUIRE(found_charlie_null);
        REQUIRE(found_david_null);
    }

    SECTION("JOIN + WHERE") {
        auto res = RunQuery(engine, "SELECT users.name, orders.amount FROM users INNER JOIN orders ON users.id = orders.user_id WHERE orders.amount > 1000;");
        REQUIRE(res.success);
        // Should return Alice (1500) and Bob (2000)
        REQUIRE(res.rows.size() == 2);
    }

    SECTION("JOIN + Projection") {
        auto res = RunQuery(engine, "SELECT users.name, orders.amount FROM users INNER JOIN orders ON users.id = orders.user_id;");
        REQUIRE(res.success);
        REQUIRE(res.schema.GetColumnCount() == 2);
        REQUIRE(res.schema.GetColumn(0).GetName() == "users.name");
        REQUIRE(res.schema.GetColumn(1).GetName() == "orders.amount");
        REQUIRE(res.rows.size() == 3);
    }

    SECTION("JOIN + ORDER BY") {
        auto res = RunQuery(engine, "SELECT users.name, orders.amount FROM users INNER JOIN orders ON users.id = orders.user_id ORDER BY orders.amount DESC;");
        REQUIRE(res.success);
        REQUIRE(res.rows.size() == 3);
        // Highest amount first: Bob (2000), Alice (1500), Alice (500)
        REQUIRE(res.rows[0].GetValue(res.schema, 1).GetAsInteger() == 2000);
        REQUIRE(res.rows[0].GetValue(res.schema, 0).GetAsVarChar() == "Bob");
        REQUIRE(res.rows[1].GetValue(res.schema, 1).GetAsInteger() == 1500);
        REQUIRE(res.rows[1].GetValue(res.schema, 0).GetAsVarChar() == "Alice");
        REQUIRE(res.rows[2].GetValue(res.schema, 1).GetAsInteger() == 500);
        REQUIRE(res.rows[2].GetValue(res.schema, 0).GetAsVarChar() == "Alice");
    }

    SECTION("JOIN + LIMIT") {
        auto res = RunQuery(engine, "SELECT users.name, orders.amount FROM users INNER JOIN orders ON users.id = orders.user_id ORDER BY orders.amount DESC LIMIT 2;");
        REQUIRE(res.success);
        REQUIRE(res.rows.size() == 2);
        REQUIRE(res.rows[0].GetValue(res.schema, 1).GetAsInteger() == 2000);
        REQUIRE(res.rows[1].GetValue(res.schema, 1).GetAsInteger() == 1500);
    }

    SECTION("Three-table JOIN") {
        REQUIRE(RunQuery(engine, "CREATE TABLE items (id INT, order_id INT, product VARCHAR);").success);
        REQUIRE(RunQuery(engine, "INSERT INTO items VALUES (1, 101, 'Book');").success);
        REQUIRE(RunQuery(engine, "INSERT INTO items VALUES (2, 102, 'Laptop');").success);

        auto res = RunQuery(engine, "SELECT users.name, items.product FROM users INNER JOIN orders ON users.id = orders.user_id INNER JOIN items ON orders.id = items.order_id;");
        REQUIRE(res.success);
        REQUIRE(res.rows.size() == 2);
    }
}
