#include <catch2/catch.hpp>
#include "forgedb/storage/disk/disk_manager.h"
#include "forgedb/storage/buffer/lru_replacer.h"
#include "forgedb/storage/buffer/buffer_pool_manager.h"
#include "forgedb/catalog/catalog.h"
#include "forgedb/execution/executor/execution_engine.h"
#include "forgedb/sql/lexer/lexer.h"
#include "forgedb/sql/parser/parser.h"
#include <filesystem>
#include <vector>

TEST_CASE("LRUReplacer: Pin, Unpin, Victim eviction", "[storage][buffer]") {
    forgedb::LRUReplacer replacer(5);

    // Initial size is 0
    REQUIRE(replacer.Size() == 0);

    // Unpin frames 1, 2, 3, 4, 5
    replacer.Unpin(1);
    replacer.Unpin(2);
    replacer.Unpin(3);
    replacer.Unpin(4);
    replacer.Unpin(5);
    REQUIRE(replacer.Size() == 5);

    // Pin frame 3 (removes from LRU)
    replacer.Pin(3);
    REQUIRE(replacer.Size() == 4);

    // Frame 1 should be the first victim (least recently unpinned)
    forgedb::frame_id_t victim_frame;
    REQUIRE(replacer.Victim(&victim_frame));
    REQUIRE(victim_frame == 1);
    REQUIRE(replacer.Size() == 3);

    // Unpin frame 2 again (it was already in replacer, shouldn't duplicate)
    replacer.Unpin(2);
    REQUIRE(replacer.Size() == 3);

    // Victim order: 2, 4, 5
    REQUIRE(replacer.Victim(&victim_frame));
    REQUIRE(victim_frame == 2);
    REQUIRE(replacer.Victim(&victim_frame));
    REQUIRE(victim_frame == 4);
    REQUIRE(replacer.Victim(&victim_frame));
    REQUIRE(victim_frame == 5);
    REQUIRE(replacer.Size() == 0);
    REQUIRE_FALSE(replacer.Victim(&victim_frame));
}

TEST_CASE("BufferPoolManager: Basic Page Lifecycle", "[storage][buffer]") {
    std::string test_db = "data/test_bpm_lifecycle.db";
    if (std::filesystem::exists(test_db)) {
        std::filesystem::remove(test_db);
    }

    forgedb::DiskManager disk_mgr(test_db);
    REQUIRE(disk_mgr.Open().ok());

    // Pool of size 3
    forgedb::BufferPoolManager bpm(3, &disk_mgr);

    forgedb::page_id_t page0, page1, page2, page3;
    auto* p0 = bpm.NewPage(&page0);
    REQUIRE(p0 != nullptr);
    REQUIRE(page0 == 0);
    std::strcpy(p0->GetData(), "Hello Page 0");

    auto* p1 = bpm.NewPage(&page1);
    REQUIRE(p1 != nullptr);
    REQUIRE(page1 == 1);
    std::strcpy(p1->GetData(), "Hello Page 1");

    auto* p2 = bpm.NewPage(&page2);
    REQUIRE(p2 != nullptr);
    REQUIRE(page2 == 2);
    std::strcpy(p2->GetData(), "Hello Page 2");

    // Pool is full of pinned pages. NewPage should fail
    auto* p_fail = bpm.NewPage(&page3);
    REQUIRE(p_fail == nullptr);

    // Unpin page 0 (dirty = true)
    REQUIRE(bpm.UnpinPage(page0, true));

    // Now page 0 can be evicted to allocate page 3
    auto* p3 = bpm.NewPage(&page3);
    REQUIRE(p3 != nullptr);
    REQUIRE(page3 == 3);
    std::strcpy(p3->GetData(), "Hello Page 3");

    // Page 0 should have been evicted to disk
    REQUIRE_FALSE(bpm.IsPageInPool(page0));

    // Unpin page 1 and page 2
    REQUIRE(bpm.UnpinPage(page1, false));
    REQUIRE(bpm.UnpinPage(page2, false));
    REQUIRE(bpm.UnpinPage(page3, true));

    // Fetch page 0 back from disk
    auto* p0_fetched = bpm.FetchPage(page0);
    REQUIRE(p0_fetched != nullptr);
    REQUIRE(std::string(p0_fetched->GetData()) == "Hello Page 0");
    REQUIRE(bpm.UnpinPage(page0, false));
}

TEST_CASE("Phase 7 Completion Gate: Correctness with buffer pool smaller than total database pages", "[storage][gate]") {
    std::string test_db = "data/gate_phase7_bpm.db";
    if (std::filesystem::exists(test_db)) {
        std::filesystem::remove(test_db);
    }

    // Step 1: Initialize database with a tiny buffer pool (only 3 frames!)
    {
        forgedb::DiskManager disk_mgr(test_db);
        REQUIRE(disk_mgr.Open().ok());

        // Pool size = 3 frames
        auto bpm = std::make_unique<forgedb::BufferPoolManager>(3, &disk_mgr);
        forgedb::Catalog catalog(bpm.get());
        REQUIRE(catalog.Init().ok());

        std::vector<forgedb::Column> cols = {
            forgedb::Column("id", forgedb::TypeId::INTEGER),
            forgedb::Column("val", forgedb::TypeId::INTEGER),
            forgedb::Column("note", forgedb::TypeId::VARCHAR, 64)
        };
        forgedb::Schema schema(std::move(cols));
        auto tbl_res = catalog.CreateTable("test_table", schema);
        REQUIRE(tbl_res.ok());
        auto* table = *tbl_res;

        // Insert 150 records across many pages (each page can hold ~30-40 records)
        // With 150 records, this spans ~5-6 pages, far exceeding the 3-frame buffer pool!
        for (int i = 0; i < 150; ++i) {
            std::vector<forgedb::Value> values = {
                forgedb::Value(i),
                forgedb::Value(i * 10),
                forgedb::Value(std::string("Entry #" + std::to_string(i)))
            };
            forgedb::Record rec(std::move(values), schema);
            REQUIRE(table->GetTableHeap()->InsertRecord(rec).ok());
        }

        // Flush all pages to disk
        bpm->FlushAllPages();
    }

    // Step 2: Re-open database with a tiny buffer pool (3 frames) and verify data integrity
    {
        forgedb::DiskManager disk_mgr(test_db);
        REQUIRE(disk_mgr.Open().ok());

        auto bpm = std::make_unique<forgedb::BufferPoolManager>(3, &disk_mgr);
        forgedb::Catalog catalog(bpm.get());
        REQUIRE(catalog.Init().ok());

        auto* table = catalog.GetTable("test_table");
        REQUIRE(table != nullptr);

        const auto& schema = table->GetSchema();
        int count = 0;
        for (auto it = table->GetTableHeap()->Begin(); it != table->GetTableHeap()->End(); ++it) {
            const auto& rec = *it;
            REQUIRE(rec.GetValue(schema, 0).GetAsInteger() == count);
            REQUIRE(rec.GetValue(schema, 1).GetAsInteger() == count * 10);
            REQUIRE(rec.GetValue(schema, 2).GetAsVarChar() == ("Entry #" + std::to_string(count)));
            ++count;
        }

        REQUIRE(count == 150);
    }
}
