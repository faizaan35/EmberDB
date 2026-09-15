#include <catch2/catch.hpp>
#include "emberdb/storage/disk/disk_manager.h"
#include "emberdb/storage/table/table_heap.h"
#include <filesystem>
#include <vector>

TEST_CASE("TableHeap multi-page insertion, scanning, and iteration", "[storage][table_heap]") {
    std::string test_db = "data/test_table_heap.db";
    if (std::filesystem::exists(test_db)) {
        std::filesystem::remove(test_db);
    }

    emberdb::Column c1("id", emberdb::TypeId::INTEGER);
    emberdb::Column c2("payload", emberdb::TypeId::VARCHAR, 255);
    emberdb::Schema schema({c1, c2});

    {
        emberdb::DiskManager disk_mgr(test_db);
        REQUIRE(disk_mgr.Open().ok());

        emberdb::TableHeap table_heap(&disk_mgr);

        // Insert enough records to force allocation of multiple slotted pages
        constexpr int NUM_RECORDS = 150;
        std::string large_string(200, 'X'); // 200 bytes each -> ~15 records per 4KB page -> ~10 pages

        for (int i = 0; i < NUM_RECORDS; ++i) {
            std::vector<emberdb::Value> vals = {
                emberdb::Value(static_cast<int32_t>(i)),
                emberdb::Value(std::to_string(i) + "_" + large_string)
            };
            emberdb::Record rec(vals, schema);
            auto status = table_heap.InsertRecord(rec);
            REQUIRE(status.ok());
            REQUIRE(rec.GetRID().IsValid());
        }

        // Verify that multiple pages were allocated
        REQUIRE(disk_mgr.GetNumPages() > 5);

        // Scan via TableIterator
        int scanned_count = 0;
        for (auto it = table_heap.Begin(); it != table_heap.End(); ++it) {
            int32_t expected_id = scanned_count;
            int32_t actual_id = it->GetValue(schema, 0).GetAsInteger();
            REQUIRE(actual_id == expected_id);
            ++scanned_count;
        }
        REQUIRE(scanned_count == NUM_RECORDS);

        REQUIRE(disk_mgr.Close().ok());
    }

    if (std::filesystem::exists(test_db)) {
        std::filesystem::remove(test_db);
    }
}
