#include <catch2/catch.hpp>
#include "forgedb/storage/disk/disk_manager.h"
#include "forgedb/catalog/catalog.h"
#include "forgedb/sql/lexer/lexer.h"
#include "forgedb/sql/parser/parser.h"
#include "forgedb/execution/executor/execution_engine.h"
#include <filesystem>

static forgedb::QueryResult RunQuery(forgedb::ExecutionEngine& engine, const std::string& sql) {
    forgedb::Lexer lexer(sql);
    auto tokens = lexer.Tokenize();
    forgedb::Parser parser(std::move(tokens));
    auto stmt_res = parser.Parse();
    if (!stmt_res.ok()) {
        return forgedb::QueryResult{false, stmt_res.status().ToString(), {}, {}, 0, 0.0};
    }
    return engine.Execute(stmt_res->get());
}

TEST_CASE("Phase 4 Completion Gate: SQL end-to-end execution", "[execution][gate]") {
    std::string test_db = "data/gate_phase4_exec.db";
    if (std::filesystem::exists(test_db)) {
        std::filesystem::remove(test_db);
    }

    forgedb::DiskManager disk_mgr(test_db);
    REQUIRE(disk_mgr.Open().ok());

    forgedb::Catalog catalog(&disk_mgr);
    REQUIRE(catalog.Init().ok());

    forgedb::ExecutionEngine engine(&catalog);

    // 1. CREATE TABLE users (id INT, name VARCHAR, age INT);
    {
        auto res = RunQuery(engine, "CREATE TABLE users (id INT, name VARCHAR, age INT);");
        REQUIRE(res.success);
        REQUIRE(catalog.HasTable("users"));
    }

    // 2. INSERT INTO users VALUES (1, 'Faizaan', 23);
    {
        auto res = RunQuery(engine, "INSERT INTO users VALUES (1, 'Faizaan', 23);");
        REQUIRE(res.success);
        REQUIRE(res.rows_affected == 1);
    }

    // 3. SELECT * FROM users;
    {
        auto res = RunQuery(engine, "SELECT * FROM users;");
        REQUIRE(res.success);
        REQUIRE(res.rows.size() == 1);
        const auto& schema = res.schema;
        REQUIRE(schema.GetColumnCount() == 3);
        REQUIRE(res.rows[0].GetValue(schema, 0).GetAsInteger() == 1);
        REQUIRE(res.rows[0].GetValue(schema, 1).GetAsVarChar() == "Faizaan");
        REQUIRE(res.rows[0].GetValue(schema, 2).GetAsInteger() == 23);
    }

    // 4. SELECT name FROM users WHERE age > 20;
    {
        auto res = RunQuery(engine, "SELECT name FROM users WHERE age > 20;");
        REQUIRE(res.success);
        REQUIRE(res.rows.size() == 1);
        REQUIRE(res.schema.GetColumnCount() == 1);
        REQUIRE(res.rows[0].GetValue(res.schema, 0).GetAsVarChar() == "Faizaan");

        // Filter that matches nothing
        auto res_empty = RunQuery(engine, "SELECT name FROM users WHERE age > 30;");
        REQUIRE(res_empty.success);
        REQUIRE(res_empty.rows.empty());
    }

    // 5. UPDATE users SET age = 24 WHERE id = 1;
    {
        auto res = RunQuery(engine, "UPDATE users SET age = 24 WHERE id = 1;");
        REQUIRE(res.success);
        REQUIRE(res.rows_affected == 1);

        // Verify update persisted
        auto verify_res = RunQuery(engine, "SELECT * FROM users;");
        REQUIRE(verify_res.success);
        REQUIRE(verify_res.rows.size() == 1);
        REQUIRE(verify_res.rows[0].GetValue(verify_res.schema, 2).GetAsInteger() == 24);
    }

    // 6. DELETE FROM users WHERE id = 1;
    {
        auto res = RunQuery(engine, "DELETE FROM users WHERE id = 1;");
        REQUIRE(res.success);
        REQUIRE(res.rows_affected == 1);

        // Verify deletion
        auto verify_res = RunQuery(engine, "SELECT * FROM users;");
        REQUIRE(verify_res.success);
        REQUIRE(verify_res.rows.empty());
    }

    // 7. Insert multiple rows, filter with complex expression, project subset
    {
        auto ins1 = RunQuery(engine, "INSERT INTO users VALUES (2, 'Ahmed', 25);");
        auto ins2 = RunQuery(engine, "INSERT INTO users VALUES (3, 'Charlie', 18);");
        auto ins3 = RunQuery(engine, "INSERT INTO users VALUES (4, 'Diana', 28);");
        REQUIRE(ins1.rows_affected == 1);
        REQUIRE(ins2.rows_affected == 1);
        REQUIRE(ins3.rows_affected == 1);

        auto sel_complex = RunQuery(engine, "SELECT id, name FROM users WHERE age >= 25 AND age <= 30;");
        REQUIRE(sel_complex.success);
        REQUIRE(sel_complex.rows.size() == 2); // Ahmed (25) and Diana (28)
        REQUIRE(sel_complex.schema.GetColumnCount() == 2);
    }

    REQUIRE(disk_mgr.Close().ok());

    if (std::filesystem::exists(test_db)) {
        std::filesystem::remove(test_db);
    }
}
