#include "catch2/catch.hpp"
#include "emberdb/catalog/catalog.h"
#include "emberdb/storage/disk/disk_manager.h"
#include "emberdb/storage/buffer/buffer_pool_manager.h"
#include "emberdb/execution/executor/execution_engine.h"
#include "emberdb/planner/planner.h"
#include "emberdb/sql/lexer/lexer.h"
#include "emberdb/sql/parser/parser.h"
#include <filesystem>
#include <vector>

using namespace emberdb;

namespace {
    const std::string PLANNER_TEST_DB = "data/test_planner.db";

    void Cleanup() {
        if (std::filesystem::exists(PLANNER_TEST_DB)) {
            std::filesystem::remove(PLANNER_TEST_DB);
        }
    }

    std::unique_ptr<Statement> ParseSQL(const std::string& sql) {
        Lexer lexer(sql);
        auto tokens = lexer.Tokenize();
        Parser parser(std::move(tokens));
        auto res = parser.Parse();
        if (!res.ok()) return nullptr;
        return std::move(*res);
    }
}

TEST_CASE("Planner: IndexScan selection, fallback to SeqScan, and result equivalence", "[planner]") {
    Cleanup();
    DiskManager disk_mgr(PLANNER_TEST_DB);
    REQUIRE(disk_mgr.Open().ok());
    BufferPoolManager bpm(64, &disk_mgr);
    Catalog catalog(&bpm);
    REQUIRE(catalog.Init().ok());
    ExecutionEngine engine(&catalog);

    // Setup table
    auto create_stmt = ParseSQL("CREATE TABLE users (id INT, name VARCHAR, age INT);");
    REQUIRE(create_stmt != nullptr);
    REQUIRE(engine.Execute(create_stmt.get()).success);

    // Insert rows (1 .. 1000)
    for (int i = 1; i <= 1000; ++i) {
        std::string sql = "INSERT INTO users VALUES (" + std::to_string(i) + ", 'User" + std::to_string(i) + "', " + std::to_string(20 + (i % 50)) + ");";
        auto ins = ParseSQL(sql);
        REQUIRE(ins != nullptr);
        REQUIRE(engine.Execute(ins.get()).success);
    }

    // -------------------------------------------------------------
    // Gate Scenario 1: Fallback to SeqScan before index creation
    // -------------------------------------------------------------
    {
        auto select_stmt = ParseSQL("SELECT * FROM users WHERE id = 500;");
        REQUIRE(select_stmt != nullptr);
        const auto* sel = dynamic_cast<const SelectStatement*>(select_stmt.get());

        auto plan = engine.GetPlanner()->PlanSelect(sel);
        REQUIRE(plan != nullptr);
        std::string plan_str = plan->ToString();
        // Without index, plan must use SeqScan and Filter
        REQUIRE(plan_str.find("SeqScan") != std::string::npos);
        REQUIRE(plan_str.find("IndexScan") == std::string::npos);

        // Execute and verify correct result
        auto qres = engine.Execute(select_stmt.get());
        REQUIRE(qres.success);
        REQUIRE(qres.rows.size() == 1);
        Table* tbl = catalog.GetTable("users");
        REQUIRE(qres.rows[0].GetValue(tbl->GetSchema(), 0).GetAsInteger() == 500);
        REQUIRE(qres.rows[0].GetValue(tbl->GetSchema(), 1).GetAsVarChar() == "User500");
    }

    // -------------------------------------------------------------
    // Create Index on users(id)
    // -------------------------------------------------------------
    auto create_idx = ParseSQL("CREATE INDEX idx_users_id ON users(id);");
    REQUIRE(create_idx != nullptr);
    REQUIRE(engine.Execute(create_idx.get()).success);

    // -------------------------------------------------------------
    // Gate Scenario 2: Automatic IndexScan selection for equality predicate
    // -------------------------------------------------------------
    {
        auto select_stmt = ParseSQL("SELECT * FROM users WHERE id = 500;");
        REQUIRE(select_stmt != nullptr);
        const auto* sel = dynamic_cast<const SelectStatement*>(select_stmt.get());

        auto plan = engine.GetPlanner()->PlanSelect(sel);
        REQUIRE(plan != nullptr);
        std::string plan_str = plan->ToString();

        // With index on id, plan MUST choose IndexScan
        REQUIRE(plan_str.find("IndexScan") != std::string::npos);
        REQUIRE(plan_str.find("idx_users_id") != std::string::npos);
        REQUIRE(plan_str.find("key=500") != std::string::npos);

        // Execute via IndexScan and verify identical correct result
        auto qres = engine.Execute(select_stmt.get());
        REQUIRE(qres.success);
        REQUIRE(qres.rows.size() == 1);
        Table* tbl = catalog.GetTable("users");
        REQUIRE(qres.rows[0].GetValue(tbl->GetSchema(), 0).GetAsInteger() == 500);
        REQUIRE(qres.rows[0].GetValue(tbl->GetSchema(), 1).GetAsVarChar() == "User500");
    }

    // -------------------------------------------------------------
    // Gate Scenario 3: Fallback to SeqScan when predicate uses unindexed column
    // -------------------------------------------------------------
    {
        auto select_stmt = ParseSQL("SELECT * FROM users WHERE age = 23;");
        REQUIRE(select_stmt != nullptr);
        const auto* sel = dynamic_cast<const SelectStatement*>(select_stmt.get());

        auto plan = engine.GetPlanner()->PlanSelect(sel);
        REQUIRE(plan != nullptr);
        std::string plan_str = plan->ToString();

        // No index on age, plan MUST fallback to SeqScan
        REQUIRE(plan_str.find("SeqScan") != std::string::npos);
        REQUIRE(plan_str.find("IndexScan") == std::string::npos);
        REQUIRE(plan_str.find("Filter") != std::string::npos);

        auto qres = engine.Execute(select_stmt.get());
        REQUIRE(qres.success);
        REQUIRE(qres.rows.size() == 20); // 1000 / 50 = 20 rows matching age = 23
    }

    // -------------------------------------------------------------
    // Gate Scenario 4: Range Scan on Index (id >= 100 AND id <= 120)
    // -------------------------------------------------------------
    {
        auto select_stmt = ParseSQL("SELECT * FROM users WHERE id >= 100 AND id <= 120;");
        REQUIRE(select_stmt != nullptr);
        const auto* sel = dynamic_cast<const SelectStatement*>(select_stmt.get());

        auto plan = engine.GetPlanner()->PlanSelect(sel);
        REQUIRE(plan != nullptr);
        std::string plan_str = plan->ToString();

        REQUIRE(plan_str.find("IndexScan") != std::string::npos);
        REQUIRE(plan_str.find("range=[100 .. 120]") != std::string::npos);

        auto qres = engine.Execute(select_stmt.get());
        REQUIRE(qres.success);
        REQUIRE(qres.rows.size() == 21);
        Table* tbl = catalog.GetTable("users");
        for (size_t i = 0; i < qres.rows.size(); ++i) {
            REQUIRE(qres.rows[i].GetValue(tbl->GetSchema(), 0).GetAsInteger() == 100 + static_cast<int>(i));
        }
    }

    // -------------------------------------------------------------
    // Gate Scenario 5: IndexScan with Residual Filter (id = 251 AND age > 20)
    // -------------------------------------------------------------
    {
        auto select_stmt = ParseSQL("SELECT * FROM users WHERE id = 251 AND age > 20;");
        REQUIRE(select_stmt != nullptr);
        const auto* sel = dynamic_cast<const SelectStatement*>(select_stmt.get());

        auto plan = engine.GetPlanner()->PlanSelect(sel);
        REQUIRE(plan != nullptr);
        std::string plan_str = plan->ToString();

        REQUIRE(plan_str.find("IndexScan") != std::string::npos);
        REQUIRE(plan_str.find("key=251") != std::string::npos);

        auto qres = engine.Execute(select_stmt.get());
        REQUIRE(qres.success);
        REQUIRE(qres.rows.size() == 1);
        Table* tbl = catalog.GetTable("users");
        REQUIRE(qres.rows[0].GetValue(tbl->GetSchema(), 0).GetAsInteger() == 251);

        // Also verify residual filter correctly discards row when condition is false
        auto false_stmt = ParseSQL("SELECT * FROM users WHERE id = 250 AND age > 20;");
        auto false_res = engine.Execute(false_stmt.get());
        REQUIRE(false_res.success);
        REQUIRE(false_res.rows.empty());
    }

    // -------------------------------------------------------------
    // Gate Scenario 6: EXPLAIN Query Statement
    // -------------------------------------------------------------
    {
        auto explain_stmt = ParseSQL("EXPLAIN SELECT * FROM users WHERE id = 500;");
        REQUIRE(explain_stmt != nullptr);

        auto qres = engine.Execute(explain_stmt.get());
        REQUIRE(qres.success);
        REQUIRE(qres.rows.size() >= 1);
        std::string plan_output = qres.rows[0].GetValue(qres.schema, 0).GetAsVarChar();
        REQUIRE(plan_output.find("IndexScan") != std::string::npos);
        REQUIRE(plan_output.find("idx_users_id") != std::string::npos);
    }

    bpm.FlushAllPages();
    disk_mgr.Close();
    Cleanup();
}
