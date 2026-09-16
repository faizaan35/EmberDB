#include <catch2/catch.hpp>
#include "emberdb/catalog/catalog.h"
#include "emberdb/execution/executor/execution_engine.h"
#include "emberdb/storage/buffer/buffer_pool_manager.h"
#include "emberdb/storage/disk/disk_manager.h"
#include "emberdb/transaction/transaction_manager.h"
#include "emberdb/index/btree/b_plus_tree_index.h"
#include "emberdb/sql/lexer/lexer.h"
#include "emberdb/sql/parser/parser.h"
#include <thread>
#include <vector>
#include <atomic>
#include <filesystem>
#include <string>

using namespace emberdb;

static const std::string CONCURRENCY_TEST_DB = "concurrency_test.db";

static void CleanupConcurrencyDb() {
    if (std::filesystem::exists(CONCURRENCY_TEST_DB)) {
        std::filesystem::remove(CONCURRENCY_TEST_DB);
    }
}

TEST_CASE("Concurrency: Thread-safe BufferPoolManager operations", "[concurrency]") {
    CleanupConcurrencyDb();
    DiskManager disk_mgr(CONCURRENCY_TEST_DB);
    REQUIRE(disk_mgr.Open().ok());

    constexpr size_t POOL_SIZE = 16;
    BufferPoolManager bpm(POOL_SIZE, &disk_mgr);

    // Pre-allocate 20 pages
    std::vector<page_id_t> page_ids;
    for (int i = 0; i < 20; ++i) {
        page_id_t pid = INVALID_PAGE_ID;
        Page* p = bpm.NewPage(&pid);
        REQUIRE(p != nullptr);
        page_ids.push_back(pid);
        bpm.UnpinPage(pid, true);
    }

    constexpr int NUM_THREADS = 8;
    constexpr int ITERS_PER_THREAD = 100;
    std::atomic<int> success_count{0};

    auto worker = [&](int thread_id) {
        for (int i = 0; i < ITERS_PER_THREAD; ++i) {
            page_id_t pid = page_ids[(thread_id * 17 + i) % page_ids.size()];
            Page* p = bpm.FetchPage(pid);
            if (p) {
                p->WLatch();
                // Safely write thread-id signature
                char* d = p->GetData();
                d[0] = static_cast<char>(thread_id);
                p->WUnlatch();

                bpm.UnpinPage(pid, true);
                success_count.fetch_add(1, std::memory_order_relaxed);
            }
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(NUM_THREADS);
    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back(worker, i);
    }

    for (auto& t : threads) {
        t.join();
    }

    REQUIRE(success_count.load() == NUM_THREADS * ITERS_PER_THREAD);

    bpm.FlushAllPages();
    disk_mgr.Close();
    CleanupConcurrencyDb();
}

TEST_CASE("Concurrency: Thread-safe TableHeap concurrent inserts and reads", "[concurrency]") {
    CleanupConcurrencyDb();
    DiskManager disk_mgr(CONCURRENCY_TEST_DB);
    REQUIRE(disk_mgr.Open().ok());
    BufferPoolManager bpm(32, &disk_mgr);

    TableHeap heap(&bpm, INVALID_PAGE_ID);
    Schema schema({
        Column("id", TypeId::INTEGER),
        Column("val", TypeId::VARCHAR, 64)
    });

    constexpr int NUM_THREADS = 4;
    constexpr int ROWS_PER_THREAD = 50;

    auto insert_worker = [&](int thread_id) {
        for (int i = 0; i < ROWS_PER_THREAD; ++i) {
            int id = thread_id * 1000 + i;
            std::string val = "Val_" + std::to_string(id);
            std::vector<Value> values = {Value(id), Value(val)};
            Record rec(std::move(values), schema);
            auto st = heap.InsertRecord(rec);
            REQUIRE(st.ok());
            REQUIRE(rec.GetRID().IsValid());
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(NUM_THREADS);
    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back(insert_worker, i);
    }

    for (auto& t : threads) {
        t.join();
    }

    // Verify all rows were inserted
    int total_rows = 0;
    for (auto it = heap.Begin(); it != heap.End(); ++it) {
        ++total_rows;
    }
    REQUIRE(total_rows == NUM_THREADS * ROWS_PER_THREAD);

    bpm.FlushAllPages();
    disk_mgr.Close();
    CleanupConcurrencyDb();
}

TEST_CASE("Concurrency: Thread-safe Catalog reader-writer synchronization", "[concurrency]") {
    CleanupConcurrencyDb();
    DiskManager disk_mgr(CONCURRENCY_TEST_DB);
    REQUIRE(disk_mgr.Open().ok());
    BufferPoolManager bpm(32, &disk_mgr);
    Catalog catalog(&bpm);
    REQUIRE(catalog.Init().ok());

    Schema schema({Column("id", TypeId::INTEGER)});

    // Pre-create some tables
    REQUIRE(catalog.CreateTable("base1", schema).ok());
    REQUIRE(catalog.CreateTable("base2", schema).ok());

    constexpr int NUM_READERS = 4;
    constexpr int NUM_WRITERS = 2;
    constexpr int ITERS = 50;
    std::atomic<bool> start_flag{false};
    std::atomic<int> read_success{0};
    std::atomic<int> write_success{0};

    auto reader = [&]() {
        while (!start_flag.load()) { std::this_thread::yield(); }
        for (int i = 0; i < ITERS; ++i) {
            auto tables = catalog.GetAllTableNames();
            REQUIRE(!tables.empty());
            REQUIRE(catalog.HasTable("base1"));
            read_success.fetch_add(1, std::memory_order_relaxed);
        }
    };

    auto writer = [&](int writer_id) {
        while (!start_flag.load()) { std::this_thread::yield(); }
        for (int i = 0; i < ITERS; ++i) {
            std::string tname = "tbl_" + std::to_string(writer_id) + "_" + std::to_string(i);
            auto res = catalog.CreateTable(tname, schema);
            if (res.ok()) {
                write_success.fetch_add(1, std::memory_order_relaxed);
            }
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_READERS; ++i) {
        threads.emplace_back(reader);
    }
    for (int i = 0; i < NUM_WRITERS; ++i) {
        threads.emplace_back(writer, i);
    }

    start_flag.store(true);
    for (auto& t : threads) {
        t.join();
    }

    REQUIRE(read_success.load() == NUM_READERS * ITERS);
    REQUIRE(write_success.load() == NUM_WRITERS * ITERS);

    bpm.FlushAllPages();
    disk_mgr.Close();
    CleanupConcurrencyDb();
}

TEST_CASE("Concurrency: Thread-safe B+ Tree Index concurrent inserts and lookups", "[concurrency]") {
    CleanupConcurrencyDb();
    DiskManager disk_mgr(CONCURRENCY_TEST_DB);
    REQUIRE(disk_mgr.Open().ok());
    BufferPoolManager bpm(64, &disk_mgr);

    BPlusTreeIndex index("test_idx", &bpm, TypeId::INTEGER);

    constexpr int NUM_THREADS = 4;
    constexpr int KEYS_PER_THREAD = 60;

    auto worker = [&](int thread_id) {
        for (int i = 0; i < KEYS_PER_THREAD; ++i) {
            int key_val = thread_id * 1000 + i;
            IndexKey k{Value(key_val)};
            RID rid(thread_id + 1, i);
            REQUIRE(index.Insert(k, rid));

            // Immediate verification
            std::vector<RID> res;
            REQUIRE(index.GetValue(k, res));
            REQUIRE(!res.empty());
            REQUIRE(res[0] == rid);
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back(worker, i);
    }

    for (auto& t : threads) {
        t.join();
    }

    // Verify all keys are reachable via ScanAll
    auto all_entries = index.ScanAll();
    REQUIRE(all_entries.size() == NUM_THREADS * KEYS_PER_THREAD);

    bpm.FlushAllPages();
    disk_mgr.Close();
    CleanupConcurrencyDb();
}

TEST_CASE("Concurrency: Concurrent Transactions with Commits and Rollbacks", "[concurrency]") {
    CleanupConcurrencyDb();
    DiskManager disk_mgr(CONCURRENCY_TEST_DB);
    REQUIRE(disk_mgr.Open().ok());
    BufferPoolManager bpm(64, &disk_mgr);
    Catalog catalog(&bpm);
    REQUIRE(catalog.Init().ok());

    Schema schema({
        Column("id", TypeId::INTEGER),
        Column("balance", TypeId::INTEGER)
    });
    auto tbl_res = catalog.CreateTable("accounts", schema);
    REQUIRE(tbl_res.ok());

    TransactionManager txn_mgr;

    constexpr int NUM_COMMITTING = 4;
    constexpr int NUM_ABORTING = 4;
    constexpr int TXN_OPS = 25;

    // Threads that commit transactions
    auto commit_worker = [&](int worker_id) {
        for (int i = 0; i < TXN_OPS; ++i) {
            auto txn = txn_mgr.Begin();
            int id = worker_id * 1000 + i;
            Record rec(std::vector<Value>{Value(id), Value(100)}, schema);
            tbl_res.value()->GetTableHeap()->InsertRecord(rec);
            txn->AppendTableWrite({TableWriteType::INSERT, "accounts", rec.GetRID(), Record(), rec});
            REQUIRE(txn_mgr.Commit(txn.get()).ok());
        }
    };

    // Threads that rollback transactions
    auto abort_worker = [&](int worker_id) {
        for (int i = 0; i < TXN_OPS; ++i) {
            auto txn = txn_mgr.Begin();
            int id = (worker_id + 50) * 1000 + i;
            Record rec(std::vector<Value>{Value(id), Value(999)}, schema);
            tbl_res.value()->GetTableHeap()->InsertRecord(rec);
            txn->AppendTableWrite({TableWriteType::INSERT, "accounts", rec.GetRID(), Record(), rec});
            REQUIRE(txn_mgr.Abort(txn.get(), &catalog).ok());
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_COMMITTING; ++i) {
        threads.emplace_back(commit_worker, i);
    }
    for (int i = 0; i < NUM_ABORTING; ++i) {
        threads.emplace_back(abort_worker, i);
    }

    for (auto& t : threads) {
        t.join();
    }

    // Verify only committed rows remain
    int valid_rows = 0;
    for (auto it = tbl_res.value()->GetTableHeap()->Begin(); it != tbl_res.value()->GetTableHeap()->End(); ++it) {
        int id = it->GetValue(schema, 0).GetAsInteger();
        REQUIRE(id < 50000); // Aborted rows (>= 50000) must NOT be present
        ++valid_rows;
    }
    REQUIRE(valid_rows == NUM_COMMITTING * TXN_OPS);

    bpm.FlushAllPages();
    disk_mgr.Close();
    CleanupConcurrencyDb();
}
