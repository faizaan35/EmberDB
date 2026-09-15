#include <catch2/catch.hpp>
#include "forgedb/storage/disk/disk_manager.h"
#include <filesystem>
#include <vector>
#include <cstring>

TEST_CASE("Phase 1 Completion Gate: Page persistence across close and reopen", "[storage][gate]") {
    std::string test_db_path = "data/gate_phase1_persistence.db";
    if (std::filesystem::exists(test_db_path)) {
        std::filesystem::remove(test_db_path);
    }

    constexpr size_t NUM_TEST_PAGES = 5;
    std::vector<std::vector<char>> expected_pages(NUM_TEST_PAGES, std::vector<char>(forgedb::PAGE_SIZE));

    // Fill each page with deterministic pseudo-random / structured data
    for (size_t p = 0; p < NUM_TEST_PAGES; ++p) {
        for (size_t i = 0; i < forgedb::PAGE_SIZE; ++i) {
            expected_pages[p][i] = static_cast<char>((p * 37 + i * 17 + 101) % 256);
        }
        // Stamp page ID marker in page header
        std::string stamp = "PAGE_ID_" + std::to_string(p) + "_TEST_STAMP";
        std::memcpy(expected_pages[p].data(), stamp.data(), stamp.size());
    }

    // Step 1: Open DiskManager, allocate pages, write page data, flush, and close
    {
        forgedb::DiskManager disk_mgr(test_db_path);
        auto open_status = disk_mgr.Open();
        REQUIRE(open_status.ok());

        for (size_t p = 0; p < NUM_TEST_PAGES; ++p) {
            auto alloc_res = disk_mgr.AllocatePage();
            REQUIRE(alloc_res.ok());
            REQUIRE(*alloc_res == static_cast<forgedb::page_id_t>(p));

            auto write_status = disk_mgr.WritePage(static_cast<forgedb::page_id_t>(p), expected_pages[p].data());
            REQUIRE(write_status.ok());
        }

        REQUIRE(disk_mgr.Flush().ok());
        REQUIRE(disk_mgr.Close().ok());
        REQUIRE_FALSE(disk_mgr.IsOpen());
    }

    // Verify file size on disk is exactly NUM_TEST_PAGES * 4096 bytes
    REQUIRE(std::filesystem::file_size(test_db_path) == NUM_TEST_PAGES * forgedb::PAGE_SIZE);

    // Step 2: Reopen DiskManager on the same database file and verify exact contents
    {
        forgedb::DiskManager disk_mgr(test_db_path);
        auto open_status = disk_mgr.Open();
        REQUIRE(open_status.ok());
        REQUIRE(disk_mgr.IsOpen());
        REQUIRE(disk_mgr.GetNumPages() == NUM_TEST_PAGES);

        std::vector<char> read_buffer(forgedb::PAGE_SIZE);
        for (size_t p = 0; p < NUM_TEST_PAGES; ++p) {
            auto read_status = disk_mgr.ReadPage(static_cast<forgedb::page_id_t>(p), read_buffer.data());
            REQUIRE(read_status.ok());

            // Byte-by-byte exact match check
            bool identical = (std::memcmp(read_buffer.data(), expected_pages[p].data(), forgedb::PAGE_SIZE) == 0);
            REQUIRE(identical);
        }

        REQUIRE(disk_mgr.Close().ok());
    }

    // Clean up
    if (std::filesystem::exists(test_db_path)) {
        std::filesystem::remove(test_db_path);
    }
}
