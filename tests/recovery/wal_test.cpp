#include <catch2/catch.hpp>
#include "emberdb/recovery/log_record.h"
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
    const std::string TEST_WAL_PATH = "test_wal.log";
    const std::string TEST_DB_PATH = "test_wal_db.db";

    void CleanupFiles() {
        std::error_code ec;
        std::filesystem::remove(TEST_WAL_PATH, ec);
        std::filesystem::remove(TEST_DB_PATH, ec);
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

TEST_CASE("LogRecord Serialization and Deserialization", "[wal]") {
    // 1. Transaction control record: BEGIN
    {
        LogRecord rec(101, INVALID_LSN, LogRecordType::BEGIN);
        rec.SetLSN(1);
        uint32_t size = rec.CalculateSize();
        std::vector<char> buf(size);
        rec.Serialize(buf.data());

        LogRecord out;
        bool ok = LogRecord::Deserialize(buf.data(), size, out);
        REQUIRE(ok);
        REQUIRE(out.GetLSN() == 1);
        REQUIRE(out.GetPrevLSN() == INVALID_LSN);
        REQUIRE(out.GetTxnId() == 101);
        REQUIRE(out.GetType() == LogRecordType::BEGIN);
    }

    // 2. INSERT record with after-image
    {
        Schema schema({
            Column("id", TypeId::INTEGER),
            Column("name", TypeId::VARCHAR, 32)
        });
        Record record({Value(42), Value("Faizaan")}, schema);
        RID rid{5, 2};

        LogRecord rec = LogRecord::CreateInsert(101, 1, "users", rid, record);
        rec.SetLSN(2);
        uint32_t size = rec.CalculateSize();
        std::vector<char> buf(size);
        rec.Serialize(buf.data());

        LogRecord out;
        bool ok = LogRecord::Deserialize(buf.data(), size, out);
        REQUIRE(ok);
        REQUIRE(out.GetLSN() == 2);
        REQUIRE(out.GetPrevLSN() == 1);
        REQUIRE(out.GetTxnId() == 101);
        REQUIRE(out.GetType() == LogRecordType::INSERT);
        REQUIRE(out.GetTableName() == "users");
        REQUIRE(out.GetRID() == rid);
        REQUIRE(out.GetAfterImage().GetValue(schema, 0).GetAsInteger() == 42);
        REQUIRE(out.GetAfterImage().GetValue(schema, 1).GetAsVarChar() == "Faizaan");
    }

    // 3. UPDATE record with before-image and after-image
    {
        Schema schema({
            Column("id", TypeId::INTEGER),
            Column("age", TypeId::INTEGER)
        });
        Record before_rec({Value(1), Value(23)}, schema);
        Record after_rec({Value(1), Value(24)}, schema);
        RID rid{3, 0};

        LogRecord rec = LogRecord::CreateUpdate(102, 5, "users", rid, before_rec, after_rec);
        rec.SetLSN(6);
        uint32_t size = rec.CalculateSize();
        std::vector<char> buf(size);
        rec.Serialize(buf.data());

        LogRecord out;
        bool ok = LogRecord::Deserialize(buf.data(), size, out);
        REQUIRE(ok);
        REQUIRE(out.GetLSN() == 6);
        REQUIRE(out.GetPrevLSN() == 5);
        REQUIRE(out.GetTxnId() == 102);
        REQUIRE(out.GetType() == LogRecordType::UPDATE);
        REQUIRE(out.GetTableName() == "users");
        REQUIRE(out.GetRID() == rid);
        REQUIRE(out.GetBeforeImage().GetValue(schema, 1).GetAsInteger() == 23);
        REQUIRE(out.GetAfterImage().GetValue(schema, 1).GetAsInteger() == 24);
    }

    // 4. DELETE record with before-image
    {
        Schema schema({
            Column("id", TypeId::INTEGER)
        });
        Record before_rec({Value(99)}, schema);
        RID rid{12, 4};

        LogRecord rec = LogRecord::CreateDelete(103, 10, "items", rid, before_rec);
        rec.SetLSN(11);
        uint32_t size = rec.CalculateSize();
        std::vector<char> buf(size);
        rec.Serialize(buf.data());

        LogRecord out;
        bool ok = LogRecord::Deserialize(buf.data(), size, out);
        REQUIRE(ok);
        REQUIRE(out.GetLSN() == 11);
        REQUIRE(out.GetPrevLSN() == 10);
        REQUIRE(out.GetTxnId() == 103);
        REQUIRE(out.GetType() == LogRecordType::DELETE);
        REQUIRE(out.GetTableName() == "items");
        REQUIRE(out.GetRID() == rid);
        REQUIRE(out.GetBeforeImage().GetValue(schema, 0).GetAsInteger() == 99);
    }
}

TEST_CASE("LogManager Append, Monotonic LSN, and Buffer Flush", "[wal]") {
    CleanupFiles();

    {
        LogManager log_mgr(TEST_WAL_PATH);
        auto st = log_mgr.Open();
        REQUIRE(st.ok());
        REQUIRE(log_mgr.IsOpen());
        REQUIRE(log_mgr.GetNextLSN() == 1);

        LogRecord rec1(1, INVALID_LSN, LogRecordType::BEGIN);
        lsn_t lsn1 = log_mgr.AppendRecord(rec1);
        REQUIRE(lsn1 == 1);
        REQUIRE(log_mgr.GetLastLSN() == 1);

        LogRecord rec2(1, lsn1, LogRecordType::COMMIT);
        lsn_t lsn2 = log_mgr.AppendRecord(rec2);
        REQUIRE(lsn2 == 2);
        REQUIRE(log_mgr.GetLastLSN() == 2);

        // Explicit flush
        log_mgr.FlushLogBuffer();
        REQUIRE(log_mgr.GetFlushedLSN() == 2);

        // Read records back from disk
        auto records = log_mgr.ReadAllRecords();
        REQUIRE(records.size() == 2);
        REQUIRE(records[0].GetLSN() == 1);
        REQUIRE(records[0].GetType() == LogRecordType::BEGIN);
        REQUIRE(records[1].GetLSN() == 2);
        REQUIRE(records[1].GetType() == LogRecordType::COMMIT);
        REQUIRE(records[1].GetPrevLSN() == 1);

        log_mgr.Close();
    }

    CleanupFiles();
}

TEST_CASE("LogManager Reopen Continues Monotonic LSN", "[wal]") {
    CleanupFiles();

    {
        LogManager log_mgr(TEST_WAL_PATH);
        REQUIRE(log_mgr.Open().ok());

        LogRecord r1(1, INVALID_LSN, LogRecordType::BEGIN);
        LogRecord r2(1, 1, LogRecordType::COMMIT);
        log_mgr.AppendRecord(r1);
        log_mgr.AppendRecord(r2);
        log_mgr.FlushLogBuffer();
        log_mgr.Close();
    }

    // Reopen
    {
        LogManager log_mgr(TEST_WAL_PATH);
        REQUIRE(log_mgr.Open().ok());
        REQUIRE(log_mgr.GetNextLSN() == 3);
        REQUIRE(log_mgr.GetFlushedLSN() == 2);

        LogRecord r3(2, INVALID_LSN, LogRecordType::BEGIN);
        lsn_t lsn3 = log_mgr.AppendRecord(r3);
        REQUIRE(lsn3 == 3);

        log_mgr.FlushLogBuffer();
        auto records = log_mgr.ReadAllRecords();
        REQUIRE(records.size() == 3);
        REQUIRE(records[2].GetLSN() == 3);
        log_mgr.Close();
    }

    CleanupFiles();
}

TEST_CASE("WAL Rule Invariant: Log Flushed Before Dirty Page Disk Write", "[wal]") {
    CleanupFiles();

    auto disk_mgr = std::make_unique<DiskManager>(TEST_DB_PATH);
    REQUIRE(disk_mgr->Open().ok());
    LogManager log_mgr(TEST_WAL_PATH);
    REQUIRE(log_mgr.Open().ok());

    auto bpm = std::make_unique<BufferPoolManager>(10, disk_mgr.get());
    bpm->SetLogManager(&log_mgr);

    // Create a new page and dirty it
    page_id_t pid = INVALID_PAGE_ID;
    Page* page = bpm->NewPage(&pid);
    REQUIRE(page != nullptr);
    REQUIRE(pid != INVALID_PAGE_ID);

    // Append some log records to log buffer without manual flush
    LogRecord r1(1, INVALID_LSN, LogRecordType::BEGIN);
    lsn_t lsn1 = log_mgr.AppendRecord(r1);

    Schema schema({Column("val", TypeId::INTEGER)});
    Record rec({Value(777)}, schema);
    LogRecord r2 = LogRecord::CreateInsert(1, lsn1, "data", RID{pid, 0}, rec);
    lsn_t lsn2 = log_mgr.AppendRecord(r2);

    // Page LSN is set to lsn2
    page->SetLSN(lsn2);
    bpm->UnpinPage(pid, true);

    // Log buffer has not been manually flushed yet
    REQUIRE(log_mgr.GetFlushedLSN() < lsn2);

    // Flush the page through BufferPoolManager
    // BufferPoolManager MUST enforce the WAL rule by flushing the WAL up to lsn2 BEFORE writing page!
    bpm->FlushPage(pid);

    // WAL invariant verified: WAL is flushed on disk up to or beyond page LSN
    REQUIRE(log_mgr.GetFlushedLSN() >= lsn2);

    log_mgr.Close();
    CleanupFiles();
}

TEST_CASE("ExecutionEngine Transactions with WAL Logging", "[wal]") {
    CleanupFiles();

    auto disk_mgr = std::make_unique<DiskManager>(TEST_DB_PATH);
    REQUIRE(disk_mgr->Open().ok());
    auto bpm = std::make_unique<BufferPoolManager>(10, disk_mgr.get());
    auto catalog = std::make_unique<Catalog>(bpm.get());
    REQUIRE(catalog->Init().ok());

    LogManager log_mgr(TEST_WAL_PATH);
    REQUIRE(log_mgr.Open().ok());

    ExecutionEngine engine(catalog.get(), &log_mgr);

    // 1. Create table
    auto p1 = ParseSQL("CREATE TABLE accounts (id INT, balance INT);");
    REQUIRE(p1 != nullptr);
    auto res1 = engine.Execute(p1.get());
    REQUIRE(res1.success);

    // 2. Transaction with INSERT, UPDATE, COMMIT
    auto p_begin = ParseSQL("BEGIN;");
    REQUIRE(engine.Execute(p_begin.get()).success);

    auto p_ins1 = ParseSQL("INSERT INTO accounts VALUES (1, 100);");
    REQUIRE(engine.Execute(p_ins1.get()).success);

    auto p_ins2 = ParseSQL("INSERT INTO accounts VALUES (2, 200);");
    REQUIRE(engine.Execute(p_ins2.get()).success);

    auto p_upd = ParseSQL("UPDATE accounts SET balance = 150 WHERE id = 1;");
    REQUIRE(engine.Execute(p_upd.get()).success);

    auto p_commit = ParseSQL("COMMIT;");
    REQUIRE(engine.Execute(p_commit.get()).success);

    // Read back all WAL records generated during execution
    auto records = log_mgr.ReadAllRecords();
    REQUIRE(records.size() >= 5); // BEGIN, INSERT, INSERT, UPDATE, COMMIT

    bool found_begin = false;
    bool found_commit = false;
    int insert_count = 0;
    int update_count = 0;

    for (const auto& rec : records) {
        if (rec.GetType() == LogRecordType::BEGIN) found_begin = true;
        if (rec.GetType() == LogRecordType::COMMIT) found_commit = true;
        if (rec.GetType() == LogRecordType::INSERT) {
            insert_count++;
            REQUIRE(rec.GetTableName() == "accounts");
        }
        if (rec.GetType() == LogRecordType::UPDATE) {
            update_count++;
            REQUIRE(rec.GetTableName() == "accounts");
        }
    }

    REQUIRE(found_begin);
    REQUIRE(found_commit);
    REQUIRE(insert_count == 2);
    REQUIRE(update_count == 1);

    // Verify ordering: COMMIT must have an LSN strictly greater than all writes
    lsn_t commit_lsn = records.back().GetLSN();
    REQUIRE(records.back().GetType() == LogRecordType::COMMIT);
    for (size_t i = 0; i < records.size() - 1; ++i) {
        REQUIRE(records[i].GetLSN() < commit_lsn);
    }

    log_mgr.Close();
    CleanupFiles();
}
