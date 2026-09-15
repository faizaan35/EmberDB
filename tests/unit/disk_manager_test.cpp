#include <catch2/catch.hpp>
#include "forgedb/storage/disk/disk_manager.h"
#include <filesystem>
#include <vector>
#include <cstring>

TEST_CASE("DiskManager page operations and boundaries", "[storage]") {
    std::string test_db_path = "data/test_disk_mgr.db";
    if (std::filesystem::exists(test_db_path)) {
        std::filesystem::remove(test_db_path);
    }

    {
        forgedb::DiskManager disk_mgr(test_db_path);
        auto open_status = disk_mgr.Open();
        REQUIRE(open_status.ok());
        REQUIRE(disk_mgr.IsOpen());
        REQUIRE(disk_mgr.GetNumPages() == 0);

        // Allocate pages
        auto p0_res = disk_mgr.AllocatePage();
        REQUIRE(p0_res.ok());
        REQUIRE(*p0_res == 0);

        auto p1_res = disk_mgr.AllocatePage();
        REQUIRE(p1_res.ok());
        REQUIRE(*p1_res == 1);

        REQUIRE(disk_mgr.GetNumPages() == 2);

        // Write to page 0
        std::vector<char> write_buf0(forgedb::PAGE_SIZE, 'A');
        std::string header0 = "FORGEDB_PAGE_ZERO";
        std::memcpy(write_buf0.data(), header0.data(), header0.size());
        auto write_status0 = disk_mgr.WritePage(0, write_buf0.data());
        REQUIRE(write_status0.ok());

        // Write to page 1
        std::vector<char> write_buf1(forgedb::PAGE_SIZE, 'B');
        std::string header1 = "FORGEDB_PAGE_ONE";
        std::memcpy(write_buf1.data(), header1.data(), header1.size());
        auto write_status1 = disk_mgr.WritePage(1, write_buf1.data());
        REQUIRE(write_status1.ok());

        // Out of bounds check
        std::vector<char> out_buf(forgedb::PAGE_SIZE, 0);
        auto oob_read = disk_mgr.ReadPage(99, out_buf.data());
        REQUIRE_FALSE(oob_read.ok());

        auto oob_write = disk_mgr.WritePage(99, out_buf.data());
        REQUIRE_FALSE(oob_write.ok());

        // Flush and close
        REQUIRE(disk_mgr.Flush().ok());
        REQUIRE(disk_mgr.Close().ok());
    }

    // Clean up
    if (std::filesystem::exists(test_db_path)) {
        std::filesystem::remove(test_db_path);
    }
}
