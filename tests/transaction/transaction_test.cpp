#include <catch2/catch.hpp>
#include "emberdb/catalog/catalog.h"
#include "emberdb/execution/executor/execution_engine.h"
#include "emberdb/sql/lexer/lexer.h"
#include "emberdb/sql/parser/parser.h"
#include "emberdb/storage/buffer/buffer_pool_manager.h"
#include "emberdb/storage/disk/disk_manager.h"
#include <filesystem>
#include <string>

using namespace emberdb;

static const std::string TXN_TEST_DB = "txn_test.db";

static void CleanupTxnDb() {
    if (std::filesystem::exists(TXN_TEST_DB)) {
        std::filesystem::remove(TXN_TEST_DB);
    }
}

static std::unique_ptr<Statement> ParseSQL(const std::string& sql) {
    Lexer lexer(sql);
    auto tokens = lexer.Tokenize();
    Parser parser(std::move(tokens));
    auto res = parser.Parse();
    if (!res.ok()) return nullptr;
    return std::move(*res);
}

TEST_CASE("Transaction: State transitions and lifecycle validation", "[transaction]") {
    TransactionManager txn_mgr;
    auto txn = txn_mgr.Begin();
    REQUIRE(txn != nullptr);
    REQUIRE(txn->GetTxnId() > 0);
    REQUIRE(txn->GetState() == TransactionState::ACTIVE);

    // Commit
    auto st = txn_mgr.Commit(txn.get());
    REQUIRE(st.ok());
    REQUIRE(txn->GetState() == TransactionState::COMMITTED);

    // Cannot commit again
    REQUIRE(!txn_mgr.Commit(txn.get()).ok());

    // New transaction for abort
    auto txn2 = txn_mgr.Begin();
    REQUIRE(txn2->GetTxnId() == txn->GetTxnId() + 1);
    REQUIRE(txn2->GetState() == TransactionState::ACTIVE);

    // Dummy catalog for abort test
    CleanupTxnDb();
    DiskManager disk_mgr(TXN_TEST_DB);
    REQUIRE(disk_mgr.Open().ok());
    BufferPoolManager bpm(16, &disk_mgr);
    Catalog catalog(&bpm);
    REQUIRE(catalog.Init().ok());

    st = txn_mgr.Abort(txn2.get(), &catalog);
    REQUIRE(st.ok());
    REQUIRE(txn2->GetState() == TransactionState::ABORTED);

    // Cannot abort again
    REQUIRE(!txn_mgr.Abort(txn2.get(), &catalog).ok());

    disk_mgr.Close();
    CleanupTxnDb();
}

TEST_CASE("Transaction: Phase 10 Gate Test — Commit, Rollback, and Persistence", "[transaction]") {
    CleanupTxnDb();

    // -------------------------------------------------------------
    // Session 1: Setup table and test rollback on INSERT
    // -------------------------------------------------------------
    {
        DiskManager disk_mgr(TXN_TEST_DB);
        REQUIRE(disk_mgr.Open().ok());
        BufferPoolManager bpm(64, &disk_mgr);
        Catalog catalog(&bpm);
        REQUIRE(catalog.Init().ok());
        ExecutionEngine engine(&catalog);

        // CREATE TABLE
        auto create_stmt = ParseSQL("CREATE TABLE users (id INT, name VARCHAR, age INT);");
        REQUIRE(create_stmt != nullptr);
        REQUIRE(engine.Execute(create_stmt.get()).success);

        // Insert initial baseline rows (auto-committed / outside explicit txn)
        auto ins1 = ParseSQL("INSERT INTO users VALUES (1, 'Alice', 25);");
        REQUIRE(engine.Execute(ins1.get()).success);
        auto ins2 = ParseSQL("INSERT INTO users VALUES (2, 'Bob', 30);");
        REQUIRE(engine.Execute(ins2.get()).success);

        // Gate Scenario 1: Rollback test on INSERT (Section 46)
        // BEGIN; INSERT INTO users VALUES (10, 'Test', 99); ROLLBACK;
        auto begin_stmt = ParseSQL("BEGIN;");
        REQUIRE(begin_stmt != nullptr);
        REQUIRE(engine.Execute(begin_stmt.get()).success);
        REQUIRE(engine.GetActiveTransaction() != nullptr);

        auto ins_rollback = ParseSQL("INSERT INTO users VALUES (10, 'Test', 99);");
        REQUIRE(engine.Execute(ins_rollback.get()).success);

        // In-flight read sees inserted row
        auto sel_inflight = ParseSQL("SELECT * FROM users WHERE id = 10;");
        auto res_inflight = engine.Execute(sel_inflight.get());
        REQUIRE(res_inflight.success);
        REQUIRE(res_inflight.rows.size() == 1);

        // Rollback
        auto rollback_stmt = ParseSQL("ROLLBACK;");
        REQUIRE(rollback_stmt != nullptr);
        REQUIRE(engine.Execute(rollback_stmt.get()).success);
        REQUIRE(engine.GetActiveTransaction() == nullptr);

        // Verify id = 10 is 0 rows after rollback
        auto sel_after = ParseSQL("SELECT * FROM users WHERE id = 10;");
        auto res_after = engine.Execute(sel_after.get());
        REQUIRE(res_after.success);
        REQUIRE(res_after.rows.empty());

        // Gate Scenario 2: Rollback test on UPDATE
        // BEGIN; UPDATE users SET age = 99 WHERE id = 1; ROLLBACK;
        REQUIRE(engine.Execute(begin_stmt.get()).success);
        auto upd_stmt = ParseSQL("UPDATE users SET age = 99 WHERE id = 1;");
        auto upd_res = engine.Execute(upd_stmt.get());
        REQUIRE(upd_res.success);
        REQUIRE(upd_res.rows_affected == 1);

        REQUIRE(engine.Execute(rollback_stmt.get()).success);

        // Verify age reverted to 25
        auto sel_u1 = ParseSQL("SELECT age FROM users WHERE id = 1;");
        auto res_u1 = engine.Execute(sel_u1.get());
        REQUIRE(res_u1.success);
        REQUIRE(res_u1.rows.size() == 1);
        Table* tbl = catalog.GetTable("users");
        REQUIRE(res_u1.rows[0].GetValue(tbl->GetSchema(), 0).GetAsInteger() == 25);

        // Gate Scenario 3: Rollback test on DELETE
        // BEGIN; DELETE FROM users WHERE id = 2; ROLLBACK;
        REQUIRE(engine.Execute(begin_stmt.get()).success);
        auto del_stmt = ParseSQL("DELETE FROM users WHERE id = 2;");
        auto del_res = engine.Execute(del_stmt.get());
        REQUIRE(del_res.success);
        REQUIRE(del_res.rows_affected == 1);

        REQUIRE(engine.Execute(rollback_stmt.get()).success);

        // Verify row id = 2 is restored
        auto sel_u2 = ParseSQL("SELECT name, age FROM users WHERE id = 2;");
        auto res_u2 = engine.Execute(sel_u2.get());
        REQUIRE(res_u2.success);
        REQUIRE(res_u2.rows.size() == 1);
        REQUIRE(res_u2.rows[0].GetValue(res_u2.schema, 0).GetAsVarChar() == "Bob");
        REQUIRE(res_u2.rows[0].GetValue(res_u2.schema, 1).GetAsInteger() == 30);

        // Gate Scenario 4: Multiple statements in transaction with commit
        // BEGIN;
        // INSERT INTO users VALUES (11, 'Committed', 45);
        // UPDATE users SET age = 26 WHERE id = 1;
        // COMMIT;
        REQUIRE(engine.Execute(begin_stmt.get()).success);
        auto ins_comm = ParseSQL("INSERT INTO users VALUES (11, 'Committed', 45);");
        REQUIRE(engine.Execute(ins_comm.get()).success);
        auto upd_comm = ParseSQL("UPDATE users SET age = 26 WHERE id = 1;");
        REQUIRE(engine.Execute(upd_comm.get()).success);

        auto commit_stmt = ParseSQL("COMMIT;");
        REQUIRE(commit_stmt != nullptr);
        REQUIRE(engine.Execute(commit_stmt.get()).success);
        REQUIRE(engine.GetActiveTransaction() == nullptr);

        // Verify changes are visible
        auto sel_comm = ParseSQL("SELECT * FROM users WHERE id = 11;");
        auto res_comm = engine.Execute(sel_comm.get());
        REQUIRE(res_comm.success);
        REQUIRE(res_comm.rows.size() == 1);

        auto sel_u1_new = ParseSQL("SELECT age FROM users WHERE id = 1;");
        auto res_u1_new = engine.Execute(sel_u1_new.get());
        REQUIRE(res_u1_new.rows[0].GetValue(tbl->GetSchema(), 0).GetAsInteger() == 26);

        // Flush and close
        bpm.FlushAllPages();
        disk_mgr.Close();
    }

    // -------------------------------------------------------------
    // Session 2: Restart database and verify committed data persists (Section 46)
    // -------------------------------------------------------------
    {
        DiskManager disk_mgr(TXN_TEST_DB);
        REQUIRE(disk_mgr.Open().ok());
        BufferPoolManager bpm(64, &disk_mgr);
        Catalog catalog(&bpm);
        REQUIRE(catalog.Init().ok());
        ExecutionEngine engine(&catalog);

        // Verify id = 11 ('Committed', 45) survived restart
        auto sel_comm = ParseSQL("SELECT id, name, age FROM users WHERE id = 11;");
        auto res_comm = engine.Execute(sel_comm.get());
        REQUIRE(res_comm.success);
        REQUIRE(res_comm.rows.size() == 1);
        Table* tbl = catalog.GetTable("users");
        REQUIRE(res_comm.rows[0].GetValue(tbl->GetSchema(), 0).GetAsInteger() == 11);
        REQUIRE(res_comm.rows[0].GetValue(tbl->GetSchema(), 1).GetAsVarChar() == "Committed");
        REQUIRE(res_comm.rows[0].GetValue(tbl->GetSchema(), 2).GetAsInteger() == 45);

        // Verify rolled-back row id = 10 is NOT present
        auto sel_rb = ParseSQL("SELECT * FROM users WHERE id = 10;");
        auto res_rb = engine.Execute(sel_rb.get());
        REQUIRE(res_rb.success);
        REQUIRE(res_rb.rows.empty());

        // Verify updated row id = 1 age == 26
        auto sel_u1 = ParseSQL("SELECT age FROM users WHERE id = 1;");
        auto res_u1 = engine.Execute(sel_u1.get());
        REQUIRE(res_u1.rows[0].GetValue(tbl->GetSchema(), 0).GetAsInteger() == 26);

        // ---------------------------------------------------------
        // Gate Scenario 5: Index synchronization on transaction rollback
        // ---------------------------------------------------------
        auto create_idx = ParseSQL("CREATE INDEX idx_users_id ON users(id);");
        REQUIRE(engine.Execute(create_idx.get()).success);

        // Point lookup through index confirms existing rows
        auto sel_idx11 = ParseSQL("SELECT name FROM users WHERE id = 11;");
        auto res_idx11 = engine.Execute(sel_idx11.get());
        REQUIRE(res_idx11.success);
        REQUIRE(res_idx11.rows.size() == 1);
        REQUIRE(res_idx11.rows[0].GetValue(res_idx11.schema, 0).GetAsVarChar() == "Committed");

        // Transaction with index: INSERT then ROLLBACK
        auto begin_stmt = ParseSQL("BEGIN TRANSACTION;");
        REQUIRE(engine.Execute(begin_stmt.get()).success);

        auto ins_idx = ParseSQL("INSERT INTO users VALUES (999, 'TempIdx', 99);");
        REQUIRE(engine.Execute(ins_idx.get()).success);

        auto rollback_stmt = ParseSQL("ROLLBACK TRANSACTION;");
        REQUIRE(engine.Execute(rollback_stmt.get()).success);

        // Index lookup for 999 must return 0 rows
        auto sel_idx999 = ParseSQL("SELECT * FROM users WHERE id = 999;");
        auto res_idx999 = engine.Execute(sel_idx999.get());
        REQUIRE(res_idx999.success);
        REQUIRE(res_idx999.rows.empty());

        // Transaction with index: DELETE then ROLLBACK
        REQUIRE(engine.Execute(begin_stmt.get()).success);
        auto del_comm = ParseSQL("DELETE FROM users WHERE id = 11;");
        REQUIRE(engine.Execute(del_comm.get()).success);

        REQUIRE(engine.Execute(rollback_stmt.get()).success);

        // Index lookup for 11 must return restored row
        auto sel_restored = ParseSQL("SELECT id, name FROM users WHERE id = 11;");
        auto res_restored = engine.Execute(sel_restored.get());
        REQUIRE(res_restored.success);
        REQUIRE(res_restored.rows.size() == 1);
        REQUIRE(res_restored.rows[0].GetValue(tbl->GetSchema(), 0).GetAsInteger() == 11);
        REQUIRE(res_restored.rows[0].GetValue(tbl->GetSchema(), 1).GetAsVarChar() == "Committed");

        // ---------------------------------------------------------
        // Gate Scenario 6: Error handling and state validation
        // ---------------------------------------------------------
        // Double BEGIN error
        REQUIRE(engine.Execute(begin_stmt.get()).success);
        auto double_begin = engine.Execute(begin_stmt.get());
        REQUIRE(!double_begin.success);
        REQUIRE(double_begin.error_message.find("already active") != std::string::npos);

        // Clean rollback
        REQUIRE(engine.Execute(rollback_stmt.get()).success);

        // COMMIT without active transaction
        auto commit_stmt = ParseSQL("COMMIT;");
        auto no_txn_commit = engine.Execute(commit_stmt.get());
        REQUIRE(!no_txn_commit.success);
        REQUIRE(no_txn_commit.error_message.find("No active transaction") != std::string::npos);

        // ROLLBACK without active transaction
        auto no_txn_rb = engine.Execute(rollback_stmt.get());
        REQUIRE(!no_txn_rb.success);
        REQUIRE(no_txn_rb.error_message.find("No active transaction") != std::string::npos);

        bpm.FlushAllPages();
        disk_mgr.Close();
    }

    CleanupTxnDb();
}
