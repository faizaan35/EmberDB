#include <catch2/catch.hpp>
#include "emberdb/storage/disk/disk_manager.h"
#include "emberdb/catalog/catalog.h"
#include <filesystem>
#include <vector>

TEST_CASE("Phase 2 Completion Gate: Table metadata and records survive process restart", "[integration][gate]") {
    std::string test_db = "data/gate_phase2_table_persistence.db";
    if (std::filesystem::exists(test_db)) {
        std::filesystem::remove(test_db);
    }

    emberdb::Column c1("id", emberdb::TypeId::INTEGER, false);
    emberdb::Column c2("username", emberdb::TypeId::VARCHAR, 64, false);
    emberdb::Column c3("balance", emberdb::TypeId::DOUBLE, true);
    emberdb::Schema schema({c1, c2, c3});

    constexpr int NUM_USERS = 80;

    // Step 1: Open database, initialize catalog, create table, insert records, close
    {
        emberdb::DiskManager disk_mgr(test_db);
        REQUIRE(disk_mgr.Open().ok());

        emberdb::Catalog catalog(&disk_mgr);
        REQUIRE(catalog.Init().ok());

        auto create_res = catalog.CreateTable("accounts", schema);
        REQUIRE(create_res.ok());
        emberdb::Table* table = *create_res;

        for (int i = 0; i < NUM_USERS; ++i) {
            std::vector<emberdb::Value> vals = {
                emberdb::Value(static_cast<int32_t>(i + 1)),
                emberdb::Value("User_" + std::to_string(i + 1)),
                emberdb::Value(100.0 + (i * 12.5))
            };
            emberdb::Record rec(vals, schema);
            auto insert_status = table->GetTableHeap()->InsertRecord(rec);
            REQUIRE(insert_status.ok());
        }

        // Flush and close
        REQUIRE(disk_mgr.Flush().ok());
        REQUIRE(disk_mgr.Close().ok());
    }

    // Step 2: Reopen database from scratch, load catalog, scan table, verify all rows
    {
        emberdb::DiskManager disk_mgr(test_db);
        REQUIRE(disk_mgr.Open().ok());

        emberdb::Catalog catalog(&disk_mgr);
        REQUIRE(catalog.Init().ok());

        REQUIRE(catalog.HasTable("accounts"));
        emberdb::Table* table = catalog.GetTable("accounts");
        REQUIRE(table != nullptr);
        REQUIRE(table->GetSchema() == schema);

        int verified_rows = 0;
        for (auto it = table->GetTableHeap()->Begin(); it != table->GetTableHeap()->End(); ++it) {
            int32_t expected_id = verified_rows + 1;
            std::string expected_user = "User_" + std::to_string(verified_rows + 1);
            double expected_bal = 100.0 + (verified_rows * 12.5);

            REQUIRE(it->GetValue(schema, 0).GetAsInteger() == expected_id);
            REQUIRE(it->GetValue(schema, 1).GetAsVarChar() == expected_user);
            REQUIRE(it->GetValue(schema, 2).GetAsDouble() == Approx(expected_bal));

            ++verified_rows;
        }

        REQUIRE(verified_rows == NUM_USERS);
        REQUIRE(disk_mgr.Close().ok());
    }

    if (std::filesystem::exists(test_db)) {
        std::filesystem::remove(test_db);
    }
}
