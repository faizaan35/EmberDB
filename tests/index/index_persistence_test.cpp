#include "catch2/catch.hpp"
#include "emberdb/catalog/catalog.h"
#include "emberdb/storage/disk/disk_manager.h"
#include "emberdb/storage/buffer/buffer_pool_manager.h"
#include "emberdb/execution/executor/execution_engine.h"
#include "emberdb/sql/lexer/lexer.h"
#include "emberdb/sql/parser/parser.h"
#include <filesystem>
#include <vector>

using namespace emberdb;

namespace {
    const std::string TEST_DB_PATH = "data/test_index_persistence.db";

    void Cleanup() {
        if (std::filesystem::exists(TEST_DB_PATH)) {
            std::filesystem::remove(TEST_DB_PATH);
        }
    }

    QueryResult RunSQL(ExecutionEngine& engine, const std::string& sql) {
        Lexer lexer(sql);
        auto tokens = lexer.Tokenize();
        Parser parser(std::move(tokens));
        auto stmt_res = parser.Parse();
        if (!stmt_res.ok()) {
            return QueryResult{false, stmt_res.status().ToString(), {}, {}, 0, 0.0};
        }
        return engine.Execute(stmt_res->get());
    }
}

TEST_CASE("B+ Tree: Persistence across process restart and CREATE INDEX integration", "[index_persistence]") {
    Cleanup();

    // -------------------------------------------------------------
    // Session 1: Create table, insert initial rows, create index, insert more rows
    // -------------------------------------------------------------
    {
        DiskManager disk_mgr(TEST_DB_PATH);
        REQUIRE(disk_mgr.Open().ok());
        BufferPoolManager bpm(64, &disk_mgr);
        Catalog catalog(&bpm);
        REQUIRE(catalog.Init().ok());
        ExecutionEngine engine(&catalog);

        // 1. Create table
        auto res = RunSQL(engine, "CREATE TABLE accounts (id INT, balance INT);");
        REQUIRE(res.success);

        // 2. Insert initial batch of rows (1 .. 250)
        for (int i = 1; i <= 250; ++i) {
            std::string sql = "INSERT INTO accounts VALUES (" + std::to_string(i) + ", " + std::to_string(i * 100) + ");";
            auto insert_res = RunSQL(engine, sql);
            REQUIRE(insert_res.success);
        }

        // 3. CREATE INDEX idx_acc_id ON accounts(id);
        // This should index all 250 existing rows!
        auto idx_res = RunSQL(engine, "CREATE INDEX idx_acc_id ON accounts(id);");
        REQUIRE(idx_res.success);

        REQUIRE(catalog.HasIndex("idx_acc_id"));
        IndexInfo* idx_info = catalog.GetIndex("idx_acc_id");
        REQUIRE(idx_info != nullptr);
        REQUIRE(idx_info->GetTableName() == "accounts");
        REQUIRE(idx_info->GetColumnName() == "id");

        // Verify existing rows are indexed
        for (int i = 1; i <= 250; ++i) {
            RID found_rid;
            REQUIRE(idx_info->GetIndex()->GetOneValue(IndexKey(i), found_rid));
            REQUIRE(found_rid.IsValid());
        }

        // 4. Insert subsequent rows (251 .. 500)
        // Automatic index maintenance on insert
        for (int i = 251; i <= 500; ++i) {
            std::string sql = "INSERT INTO accounts VALUES (" + std::to_string(i) + ", " + std::to_string(i * 100) + ");";
            auto insert_res = RunSQL(engine, sql);
            REQUIRE(insert_res.success);
        }

        // Verify all 500 rows are present in the index
        for (int i = 1; i <= 500; ++i) {
            RID found_rid;
            REQUIRE(idx_info->GetIndex()->GetOneValue(IndexKey(i), found_rid));
            REQUIRE(found_rid.IsValid());
        }

        // Flush all dirty pages to disk and close
        bpm.FlushAllPages();
        disk_mgr.Close();
    }

    // -------------------------------------------------------------
    // Session 2: Reopen database and verify index persistence
    // -------------------------------------------------------------
    {
        DiskManager disk_mgr(TEST_DB_PATH);
        REQUIRE(disk_mgr.Open().ok());
        BufferPoolManager bpm(64, &disk_mgr);
        Catalog catalog(&bpm);
        REQUIRE(catalog.Init().ok());
        ExecutionEngine engine(&catalog);

        // Verify catalog restored the index
        REQUIRE(catalog.HasIndex("idx_acc_id"));
        IndexInfo* idx_info = catalog.GetIndex("idx_acc_id");
        REQUIRE(idx_info != nullptr);
        REQUIRE(idx_info->GetTableName() == "accounts");
        REQUIRE(idx_info->GetColumnName() == "id");
        REQUIRE(idx_info->GetIndex()->GetRootPageId() != INVALID_PAGE_ID);

        // Verify exact lookups for all 500 rows from session 1
        for (int i = 1; i <= 500; ++i) {
            RID found_rid;
            REQUIRE(idx_info->GetIndex()->GetOneValue(IndexKey(i), found_rid));
            REQUIRE(found_rid.IsValid());

            // Cross-verify with TableHeap: fetch the actual tuple using the index RID
            Table* tbl = catalog.GetTable("accounts");
            Record rec;
            auto s = tbl->GetTableHeap()->GetRecord(found_rid, rec);
            REQUIRE(s.ok());
            REQUIRE(rec.GetValue(tbl->GetSchema(), 0).GetAsInteger() == i);
            REQUIRE(rec.GetValue(tbl->GetSchema(), 1).GetAsInteger() == i * 100);
        }

        // Range scan on reopened index: [150 .. 250] -> 101 items
        auto range_items = idx_info->GetIndex()->ScanRange(IndexKey(150), IndexKey(250));
        REQUIRE(range_items.size() == 101);
        for (size_t i = 0; i < range_items.size(); ++i) {
            int expected_id = 150 + static_cast<int>(i);
            REQUIRE(range_items[i].first == IndexKey(expected_id));
        }

        // Insert new rows in Session 2 (501 .. 520)
        for (int i = 501; i <= 520; ++i) {
            std::string sql = "INSERT INTO accounts VALUES (" + std::to_string(i) + ", " + std::to_string(i * 100) + ");";
            auto insert_res = RunSQL(engine, sql);
            REQUIRE(insert_res.success);
        }

        // Verify new rows can be queried from index
        for (int i = 501; i <= 520; ++i) {
            RID found_rid;
            REQUIRE(idx_info->GetIndex()->GetOneValue(IndexKey(i), found_rid));
            REQUIRE(found_rid.IsValid());
        }

        bpm.FlushAllPages();
        disk_mgr.Close();
    }

    Cleanup();
}
