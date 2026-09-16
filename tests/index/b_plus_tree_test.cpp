#include "catch2/catch.hpp"
#include "emberdb/index/btree/b_plus_tree_index.h"
#include "emberdb/index/index_key.h"
#include "emberdb/storage/disk/disk_manager.h"
#include "emberdb/storage/buffer/buffer_pool_manager.h"
#include <filesystem>
#include <vector>
#include <random>
#include <algorithm>

using namespace emberdb;

namespace {
    const std::string TEST_DB = "data/test_b_plus_tree.db";

    void Cleanup() {
        if (std::filesystem::exists(TEST_DB)) {
            std::filesystem::remove(TEST_DB);
        }
    }
}

TEST_CASE("B+ Tree: 1 insertion, lookup, and root creation", "[btree]") {
    Cleanup();
    DiskManager disk_mgr(TEST_DB);
    REQUIRE(disk_mgr.Open().ok());
    BufferPoolManager bpm(32, &disk_mgr);

    BPlusTreeIndex index("idx_single", &bpm, TypeId::INTEGER);
    REQUIRE(index.IsEmpty());
    REQUIRE(index.GetRootPageId() == INVALID_PAGE_ID);

    // 1. Insert single entry
    IndexKey key1(42);
    RID rid1(1, 5);
    REQUIRE(index.Insert(key1, rid1));
    REQUIRE_FALSE(index.IsEmpty());
    REQUIRE(index.GetRootPageId() != INVALID_PAGE_ID);

    // 2. Exact lookup
    RID found_rid;
    REQUIRE(index.GetOneValue(key1, found_rid));
    REQUIRE(found_rid == rid1);

    // 3. Absent key lookup
    RID absent_rid;
    REQUIRE_FALSE(index.GetOneValue(IndexKey(999), absent_rid));

    // 4. Range scan
    auto scan_res = index.ScanRange(IndexKey(40), IndexKey(50));
    REQUIRE(scan_res.size() == 1);
    REQUIRE(scan_res[0].first == key1);
    REQUIRE(scan_res[0].second == rid1);

    bpm.FlushAllPages();
    disk_mgr.Close();
    Cleanup();
}

TEST_CASE("B+ Tree: 100 insertions, leaf splits, and range scan", "[btree]") {
    Cleanup();
    DiskManager disk_mgr(TEST_DB);
    REQUIRE(disk_mgr.Open().ok());
    BufferPoolManager bpm(64, &disk_mgr);

    BPlusTreeIndex index("idx_100", &bpm, TypeId::INTEGER);

    // Insert 100 keys (with leaf capacity = 29, 100 entries will trigger multiple leaf splits)
    for (int32_t i = 1; i <= 100; ++i) {
        IndexKey key(i);
        RID rid(i / 10, i % 10);
        REQUIRE(index.Insert(key, rid));
    }

    // Verify all 100 keys are found exactly
    for (int32_t i = 1; i <= 100; ++i) {
        RID found;
        REQUIRE(index.GetOneValue(IndexKey(i), found));
        REQUIRE(found == RID(i / 10, i % 10));
    }

    // Verify non-existent keys
    RID dummy;
    REQUIRE_FALSE(index.GetOneValue(IndexKey(0), dummy));
    REQUIRE_FALSE(index.GetOneValue(IndexKey(101), dummy));
    REQUIRE_FALSE(index.GetOneValue(IndexKey(-5), dummy));

    // Range scan: [25, 75] -> 51 entries
    auto range_res = index.ScanRange(IndexKey(25), IndexKey(75));
    REQUIRE(range_res.size() == 51);
    for (size_t i = 0; i < range_res.size(); ++i) {
        int32_t expected_val = 25 + static_cast<int32_t>(i);
        REQUIRE(range_res[i].first == IndexKey(expected_val));
        REQUIRE(range_res[i].second == RID(expected_val / 10, expected_val % 10));
    }

    // Full scan: all 100 entries in ascending sorted order
    auto all_res = index.ScanAll();
    REQUIRE(all_res.size() == 100);
    for (int32_t i = 0; i < 100; ++i) {
        REQUIRE(all_res[i].first == IndexKey(i + 1));
    }

    bpm.FlushAllPages();
    disk_mgr.Close();
    Cleanup();
}

TEST_CASE("B+ Tree: 1,000 insertions, internal splits, root splitting, and full sort order", "[btree]") {
    Cleanup();
    DiskManager disk_mgr(TEST_DB);
    REQUIRE(disk_mgr.Open().ok());
    BufferPoolManager bpm(128, &disk_mgr);

    BPlusTreeIndex index("idx_1000", &bpm, TypeId::INTEGER);

    page_id_t initial_root = INVALID_PAGE_ID;
    int root_split_count = 0;
    index.SetOnRootPageChange([&](page_id_t new_root) {
        if (initial_root == INVALID_PAGE_ID) {
            initial_root = new_root;
        } else {
            root_split_count++;
        }
    });

    // Shuffle 1..1000 to test randomized insertion order with splits at various branches
    std::vector<int32_t> keys(1000);
    for (int32_t i = 0; i < 1000; ++i) {
        keys[i] = i + 1;
    }
    std::mt19937 g(42);
    std::shuffle(keys.begin(), keys.end(), g);

    for (int32_t k : keys) {
        REQUIRE(index.Insert(IndexKey(k), RID(k, k * 2)));
    }

    // Verify tree height grew and root split multiple times
    REQUIRE(root_split_count >= 2);

    // Verify all 1,000 keys exist with exact expected RIDs
    for (int32_t i = 1; i <= 1000; ++i) {
        RID found;
        REQUIRE(index.GetOneValue(IndexKey(i), found));
        REQUIRE(found == RID(i, i * 2));
    }

    // Verify range scan across multiple internal and leaf splits
    auto range_res = index.ScanRange(IndexKey(200), IndexKey(399));
    REQUIRE(range_res.size() == 200);
    for (int32_t i = 0; i < 200; ++i) {
        REQUIRE(range_res[i].first == IndexKey(200 + i));
        REQUIRE(range_res[i].second == RID(200 + i, (200 + i) * 2));
    }

    // Verify full scan produces strictly ascending order from 1 to 1000
    auto all_res = index.ScanAll();
    REQUIRE(all_res.size() == 1000);
    for (int32_t i = 0; i < 1000; ++i) {
        REQUIRE(all_res[i].first == IndexKey(i + 1));
        REQUIRE(all_res[i].second == RID(i + 1, (i + 1) * 2));
    }

    bpm.FlushAllPages();
    disk_mgr.Close();
    Cleanup();
}

TEST_CASE("B+ Tree: VARCHAR key support and string ordering", "[btree]") {
    Cleanup();
    DiskManager disk_mgr(TEST_DB);
    REQUIRE(disk_mgr.Open().ok());
    BufferPoolManager bpm(64, &disk_mgr);

    BPlusTreeIndex index("idx_varchar", &bpm, TypeId::VARCHAR);

    std::vector<std::string> names = {
        "Faizaan", "Ahmed", "Charlie", "David", "Eve", "Frank", "Grace",
        "Heidi", "Ivan", "Judy", "Mallory", "Niaj", "Olivia", "Peggy",
        "Sybil", "Trent", "Victor", "Walter", "Wendy"
    };

    for (size_t i = 0; i < names.size(); ++i) {
        REQUIRE(index.Insert(IndexKey(names[i]), RID(1, static_cast<slot_id_t>(i))));
    }

    // Lookups
    for (size_t i = 0; i < names.size(); ++i) {
        RID found;
        REQUIRE(index.GetOneValue(IndexKey(names[i]), found));
        REQUIRE(found == RID(1, static_cast<slot_id_t>(i)));
    }

    // Full scan in alphabetical order
    auto all_res = index.ScanAll();
    REQUIRE(all_res.size() == names.size());

    std::vector<std::string> sorted_names = names;
    std::sort(sorted_names.begin(), sorted_names.end());

    for (size_t i = 0; i < sorted_names.size(); ++i) {
        REQUIRE(all_res[i].first == IndexKey(sorted_names[i]));
    }

    bpm.FlushAllPages();
    disk_mgr.Close();
    Cleanup();
}
