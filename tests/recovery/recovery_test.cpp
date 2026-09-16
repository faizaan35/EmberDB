#include <catch2/catch.hpp>
#include "emberdb/recovery/recovery_manager.h"
#include "emberdb/recovery/log_manager.h"
#include "emberdb/storage/buffer/buffer_pool_manager.h"
#include "emberdb/storage/disk/disk_manager.h"
#include "emberdb/catalog/catalog.h"
#include "emberdb/execution/executor/execution_engine.h"
#include "emberdb/sql/lexer/lexer.h"
#include "emberdb/sql/parser/parser.h"
#include <filesystem>
#include <vector>

using namespace emberdb;

namespace {
    const std::string REC_TEST_WAL = "rec_test.wal";
    const std::string REC_TEST_DB = "rec_test.db";

    void CleanupRecFiles() {
        std::error_code ec;
        std::filesystem::remove(REC_TEST_WAL, ec);
        std::filesystem::remove(REC_TEST_DB, ec);
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

TEST_CASE("Crash Recovery: Clean Shutdown vs Unclean Shutdown Detection", "[recovery]") {
    CleanupRecFiles();

    // 1. Clean shutdown case
    {
        DiskManager disk_mgr(REC_TEST_DB);
        REQUIRE(disk_mgr.Open().ok());
        BufferPoolManager bpm(16, &disk_mgr);
        Catalog catalog(&bpm);
        REQUIRE(catalog.Init().ok());

        LogManager log_mgr(REC_TEST_WAL);
        REQUIRE(log_mgr.Open().ok());
        bpm.SetLogManager(&log_mgr);

        RecoveryManager recovery(&disk_mgr, &bpm, &catalog, &log_mgr);
        REQUIRE(recovery.RecordCleanShutdown().ok());
        log_mgr.Close();
        disk_mgr.Close();
    }

    // Reopen after clean shutdown
    {
        DiskManager disk_mgr(REC_TEST_DB);
        REQUIRE(disk_mgr.Open().ok());
        BufferPoolManager bpm(16, &disk_mgr);
        Catalog catalog(&bpm);
        REQUIRE(catalog.Init().ok());

        LogManager log_mgr(REC_TEST_WAL);
        REQUIRE(log_mgr.Open().ok());

        RecoveryManager recovery(&disk_mgr, &bpm, &catalog, &log_mgr);
        REQUIRE_FALSE(recovery.NeedsRecovery());

        log_mgr.Close();
        disk_mgr.Close();
    }

    // 2. Unclean shutdown case: active transaction left uncommitted
    {
        DiskManager disk_mgr(REC_TEST_DB);
        REQUIRE(disk_mgr.Open().ok());
        BufferPoolManager bpm(16, &disk_mgr);
        Catalog catalog(&bpm);
        REQUIRE(catalog.Init().ok());

        LogManager log_mgr(REC_TEST_WAL);
        REQUIRE(log_mgr.Open().ok());
        bpm.SetLogManager(&log_mgr);

        // Start transaction and append uncommitted insert log
        LogRecord r1 = LogRecord::CreateBegin(999);
        log_mgr.AppendRecord(r1);
        LogRecord r2 = LogRecord::CreateInsert(999, r1.GetLSN(), "users", RID{1, 0}, Record());
        log_mgr.AppendRecord(r2);
        log_mgr.FlushLogBuffer();

        // Simulate crash: abrupt close without clean shutdown record
        log_mgr.Close();
        disk_mgr.Close();
    }

    // Reopen after crash: must detect unclean shutdown
    {
        DiskManager disk_mgr(REC_TEST_DB);
        REQUIRE(disk_mgr.Open().ok());
        BufferPoolManager bpm(16, &disk_mgr);
        Catalog catalog(&bpm);
        REQUIRE(catalog.Init().ok());

        LogManager log_mgr(REC_TEST_WAL);
        REQUIRE(log_mgr.Open().ok());

        RecoveryManager recovery(&disk_mgr, &bpm, &catalog, &log_mgr);
        REQUIRE(recovery.NeedsRecovery());

        log_mgr.Close();
        disk_mgr.Close();
    }

    CleanupRecFiles();
}

TEST_CASE("Crash Recovery: Redo Committed Transactions After Crash", "[recovery]") {
    CleanupRecFiles();

    Schema schema({
        Column("id", TypeId::INTEGER),
        Column("val", TypeId::INTEGER)
    });

    // Session 1: Create table and flush catalog, then simulate committed transactions in WAL
    // but crash before dirty data page is written to disk
    page_id_t data_page_id = INVALID_PAGE_ID;
    {
        DiskManager disk_mgr(REC_TEST_DB);
        REQUIRE(disk_mgr.Open().ok());
        BufferPoolManager bpm(16, &disk_mgr);
        Catalog catalog(&bpm);
        REQUIRE(catalog.Init().ok());

        auto create_res = catalog.CreateTable("items", schema);
        REQUIRE(create_res.ok());
        bpm.FlushAllPages(); // Table metadata safely on disk

        LogManager log_mgr(REC_TEST_WAL);
        REQUIRE(log_mgr.Open().ok());
        bpm.SetLogManager(&log_mgr);

        // Allocate a data page
        Page* page = bpm.NewPage(&data_page_id);
        REQUIRE(page != nullptr);
        SlottedPage sp(page->GetData());
        sp.Init(data_page_id);
        bpm.UnpinPage(data_page_id, true);
        bpm.FlushAllPages(); // Initialize page frame on disk

        // Now append WAL records for committed transaction T100 inserting (1, 500)
        LogRecord b = LogRecord::CreateBegin(100);
        lsn_t lsn_b = log_mgr.AppendRecord(b);

        Record r({Value(1), Value(500)}, schema);
        RID rid{data_page_id, 0};
        LogRecord ins = LogRecord::CreateInsert(100, lsn_b, "items", rid, r);
        lsn_t lsn_ins = log_mgr.AppendRecord(ins);

        LogRecord comm = LogRecord::CreateCommit(100, lsn_ins);
        lsn_t lsn_comm = log_mgr.AppendRecord(comm);
        log_mgr.FlushLogBufferUpTo(lsn_comm);

        // Crash simulation: log has the committed transaction, but page on disk was NOT updated!
        log_mgr.Close();
        disk_mgr.Close();
    }

    // Session 2: Restart and recover
    {
        DiskManager disk_mgr(REC_TEST_DB);
        REQUIRE(disk_mgr.Open().ok());
        BufferPoolManager bpm(16, &disk_mgr);
        Catalog catalog(&bpm);
        REQUIRE(catalog.Init().ok());

        LogManager log_mgr(REC_TEST_WAL);
        REQUIRE(log_mgr.Open().ok());
        bpm.SetLogManager(&log_mgr);

        RecoveryManager recovery(&disk_mgr, &bpm, &catalog, &log_mgr);
        REQUIRE(recovery.NeedsRecovery());

        auto rec_res = recovery.Recover();
        REQUIRE(rec_res.ok());
        RecoveryStats stats = *rec_res;
        REQUIRE(stats.committed_txns_count == 1);
        REQUIRE(stats.redone_records >= 1);

        // Verify that the record is now readable from TableHeap
        Table* table = catalog.GetTable("items");
        REQUIRE(table != nullptr);

        Record retrieved;
        auto st = table->GetTableHeap()->GetRecord(RID{data_page_id, 0}, retrieved);
        REQUIRE(st.ok());
        REQUIRE(retrieved.GetValue(schema, 0).GetAsInteger() == 1);
        REQUIRE(retrieved.GetValue(schema, 1).GetAsInteger() == 500);

        recovery.RecordCleanShutdown();
        log_mgr.Close();
        disk_mgr.Close();
    }

    CleanupRecFiles();
}

TEST_CASE("Crash Recovery: Undo Uncommitted (Loser) Transactions", "[recovery]") {
    CleanupRecFiles();

    Schema schema({
        Column("id", TypeId::INTEGER),
        Column("val", TypeId::INTEGER)
    });

    {
        DiskManager disk_mgr(REC_TEST_DB);
        REQUIRE(disk_mgr.Open().ok());
        BufferPoolManager bpm(16, &disk_mgr);
        Catalog catalog(&bpm);
        REQUIRE(catalog.Init().ok());

        auto create_res = catalog.CreateTable("users", schema);
        REQUIRE(create_res.ok());
        bpm.FlushAllPages();

        LogManager log_mgr(REC_TEST_WAL);
        REQUIRE(log_mgr.Open().ok());
        bpm.SetLogManager(&log_mgr);

        ExecutionEngine engine(&catalog, &log_mgr);

        // Committed baseline transaction T1
        auto p_b1 = ParseSQL("BEGIN;");
        REQUIRE(engine.Execute(p_b1.get()).success);
        auto p_i1 = ParseSQL("INSERT INTO users VALUES (1, 100);");
        REQUIRE(engine.Execute(p_i1.get()).success);
        auto p_c1 = ParseSQL("COMMIT;");
        REQUIRE(engine.Execute(p_c1.get()).success);

        // Now start uncommitted loser transaction T2
        auto p_b2 = ParseSQL("BEGIN;");
        REQUIRE(engine.Execute(p_b2.get()).success);
        auto p_i2 = ParseSQL("INSERT INTO users VALUES (2, 200);");
        REQUIRE(engine.Execute(p_i2.get()).success);
        auto p_u1 = ParseSQL("UPDATE users SET val = 999 WHERE id = 1;");
        REQUIRE(engine.Execute(p_u1.get()).success);

        // Flush dirty pages and log to disk to simulate an abrupt crash
        // while T2 is ACTIVE (dirty pages reached disk before crash)
        bpm.FlushAllPages();
        log_mgr.FlushLogBuffer();

        // Simulate crash: no COMMIT for T2, sudden process termination
        log_mgr.Close();
        disk_mgr.Close();
    }

    // Session 2: Restart and recover
    {
        DiskManager disk_mgr(REC_TEST_DB);
        REQUIRE(disk_mgr.Open().ok());
        BufferPoolManager bpm(16, &disk_mgr);
        Catalog catalog(&bpm);
        REQUIRE(catalog.Init().ok());

        LogManager log_mgr(REC_TEST_WAL);
        REQUIRE(log_mgr.Open().ok());
        bpm.SetLogManager(&log_mgr);

        RecoveryManager recovery(&disk_mgr, &bpm, &catalog, &log_mgr);
        REQUIRE(recovery.NeedsRecovery());

        auto rec_res = recovery.Recover();
        REQUIRE(rec_res.ok());
        RecoveryStats stats = *rec_res;
        REQUIRE(stats.shutdown_status == ShutdownStatus::UNCLEAN);
        REQUIRE(stats.active_txns_count >= 1); // T2 was active
        REQUIRE(stats.undone_records >= 2);   // Insert of 2 and Update of 1 undone!

        ExecutionEngine engine(&catalog, &log_mgr);

        // Verify:
        // 1. Row (1, 100) must be preserved with its original value (100, not 999)!
        // 2. Row 2 must NOT exist!
        auto sel1 = ParseSQL("SELECT * FROM users WHERE id = 1;");
        auto res1 = engine.Execute(sel1.get());
        REQUIRE(res1.success);
        REQUIRE(res1.rows.size() == 1);
        REQUIRE(res1.rows[0].GetValue(schema, 1).GetAsInteger() == 100);

        auto sel2 = ParseSQL("SELECT * FROM users WHERE id = 2;");
        auto res2 = engine.Execute(sel2.get());
        REQUIRE(res2.success);
        REQUIRE(res2.rows.empty());

        recovery.RecordCleanShutdown();
        log_mgr.Close();
        disk_mgr.Close();
    }

    CleanupRecFiles();
}

TEST_CASE("Crash Recovery: End-to-End Continued Operations After Recovery", "[recovery]") {
    CleanupRecFiles();

    // Session 1: Run transactions and simulate crash
    {
        DiskManager disk_mgr(REC_TEST_DB);
        REQUIRE(disk_mgr.Open().ok());
        BufferPoolManager bpm(16, &disk_mgr);
        Catalog catalog(&bpm);
        REQUIRE(catalog.Init().ok());

        LogManager log_mgr(REC_TEST_WAL);
        REQUIRE(log_mgr.Open().ok());
        bpm.SetLogManager(&log_mgr);

        ExecutionEngine engine(&catalog, &log_mgr);

        REQUIRE(engine.Execute(ParseSQL("CREATE TABLE accounts (id INT, balance INT);").get()).success);
        REQUIRE(engine.Execute(ParseSQL("CREATE INDEX idx_acc_id ON accounts(id);").get()).success);

        // T1 committed
        REQUIRE(engine.Execute(ParseSQL("BEGIN;").get()).success);
        REQUIRE(engine.Execute(ParseSQL("INSERT INTO accounts VALUES (10, 1000);").get()).success);
        REQUIRE(engine.Execute(ParseSQL("COMMIT;").get()).success);

        // T2 crashed mid-flight
        REQUIRE(engine.Execute(ParseSQL("BEGIN;").get()).success);
        REQUIRE(engine.Execute(ParseSQL("INSERT INTO accounts VALUES (20, 2000);").get()).success);
        bpm.FlushAllPages();
        log_mgr.FlushLogBuffer();

        // Abrupt crash
        log_mgr.Close();
        disk_mgr.Close();
    }

    // Session 2: Recover and resume continuous operations
    {
        DiskManager disk_mgr(REC_TEST_DB);
        REQUIRE(disk_mgr.Open().ok());
        BufferPoolManager bpm(16, &disk_mgr);
        Catalog catalog(&bpm);
        REQUIRE(catalog.Init().ok());

        LogManager log_mgr(REC_TEST_WAL);
        REQUIRE(log_mgr.Open().ok());
        bpm.SetLogManager(&log_mgr);

        RecoveryManager recovery(&disk_mgr, &bpm, &catalog, &log_mgr);
        REQUIRE(recovery.NeedsRecovery());
        auto rec_res = recovery.Recover();
        REQUIRE(rec_res.ok());

        // Now resume normal execution
        ExecutionEngine engine(&catalog, &log_mgr);

        // Query committed data using index scan
        auto sel1 = ParseSQL("SELECT balance FROM accounts WHERE id = 10;");
        auto r1 = engine.Execute(sel1.get());
        REQUIRE(r1.success);
        REQUIRE(r1.rows.size() == 1);
        REQUIRE(r1.rows[0].GetValue(catalog.GetTable("accounts")->GetSchema(), 0).GetAsInteger() == 1000);

        // Uncommitted data is absent
        auto sel2 = ParseSQL("SELECT * FROM accounts WHERE id = 20;");
        auto r2 = engine.Execute(sel2.get());
        REQUIRE(r2.success);
        REQUIRE(r2.rows.empty());

        // Execute new statements after recovery
        auto ins_new = ParseSQL("INSERT INTO accounts VALUES (30, 3000);");
        REQUIRE(engine.Execute(ins_new.get()).success);

        auto sel3 = ParseSQL("SELECT balance FROM accounts WHERE id = 30;");
        auto r3 = engine.Execute(sel3.get());
        REQUIRE(r3.success);
        REQUIRE(r3.rows.size() == 1);
        REQUIRE(r3.rows[0].GetValue(catalog.GetTable("accounts")->GetSchema(), 0).GetAsInteger() == 3000);

        // Clean shutdown
        recovery.RecordCleanShutdown();
        log_mgr.Close();
        disk_mgr.Close();
    }

    // Session 3: Verify clean shutdown persisted everything bit-for-bit
    {
        DiskManager disk_mgr(REC_TEST_DB);
        REQUIRE(disk_mgr.Open().ok());
        BufferPoolManager bpm(16, &disk_mgr);
        Catalog catalog(&bpm);
        REQUIRE(catalog.Init().ok());

        LogManager log_mgr(REC_TEST_WAL);
        REQUIRE(log_mgr.Open().ok());
        bpm.SetLogManager(&log_mgr);

        RecoveryManager recovery(&disk_mgr, &bpm, &catalog, &log_mgr);
        REQUIRE_FALSE(recovery.NeedsRecovery());

        ExecutionEngine engine(&catalog, &log_mgr);
        auto sel_all = ParseSQL("SELECT * FROM accounts ORDER BY id ASC;");
        auto r = engine.Execute(sel_all.get());
        REQUIRE(r.success);
        REQUIRE(r.rows.size() == 2); // id 10 and id 30

        log_mgr.Close();
        disk_mgr.Close();
    }

    CleanupRecFiles();
}
