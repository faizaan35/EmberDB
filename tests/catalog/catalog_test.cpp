#include <catch2/catch.hpp>
#include "forgedb/storage/disk/disk_manager.h"
#include "forgedb/catalog/catalog.h"
#include <filesystem>

TEST_CASE("Catalog table creation, schema persistence, and lookup", "[catalog]") {
    std::string test_db = "data/test_catalog.db";
    if (std::filesystem::exists(test_db)) {
        std::filesystem::remove(test_db);
    }

    forgedb::Column c1("id", forgedb::TypeId::INTEGER);
    forgedb::Column c2("name", forgedb::TypeId::VARCHAR, 50);
    forgedb::Schema user_schema({c1, c2});

    forgedb::Column o1("order_id", forgedb::TypeId::BIGINT);
    forgedb::Column o2("total", forgedb::TypeId::DOUBLE);
    forgedb::Schema order_schema({o1, o2});

    // Step 1: Create catalog and tables
    {
        forgedb::DiskManager disk_mgr(test_db);
        REQUIRE(disk_mgr.Open().ok());

        forgedb::Catalog catalog(&disk_mgr);
        REQUIRE(catalog.Init().ok());

        auto res_users = catalog.CreateTable("users", user_schema);
        REQUIRE(res_users.ok());
        REQUIRE((*res_users)->GetName() == "users");

        auto res_orders = catalog.CreateTable("orders", order_schema);
        REQUIRE(res_orders.ok());
        REQUIRE((*res_orders)->GetName() == "orders");

        // Duplicate table check
        auto dup_res = catalog.CreateTable("users", user_schema);
        REQUIRE_FALSE(dup_res.ok());

        REQUIRE(catalog.HasTable("users"));
        REQUIRE(catalog.HasTable("orders"));
        REQUIRE_FALSE(catalog.HasTable("non_existent"));

        REQUIRE(disk_mgr.Close().ok());
    }

    // Step 2: Reopen catalog from disk and verify persisted metadata
    {
        forgedb::DiskManager disk_mgr(test_db);
        REQUIRE(disk_mgr.Open().ok());

        forgedb::Catalog catalog(&disk_mgr);
        REQUIRE(catalog.Init().ok());

        REQUIRE(catalog.HasTable("users"));
        REQUIRE(catalog.HasTable("orders"));

        auto* tbl_users = catalog.GetTable("users");
        REQUIRE(tbl_users != nullptr);
        REQUIRE(tbl_users->GetSchema().GetColumnCount() == 2);
        REQUIRE(tbl_users->GetSchema().GetColumn(0).GetName() == "id");
        REQUIRE(tbl_users->GetSchema().GetColumn(1).GetName() == "name");

        auto* tbl_orders = catalog.GetTable("orders");
        REQUIRE(tbl_orders != nullptr);
        REQUIRE(tbl_orders->GetSchema().GetColumnCount() == 2);
        REQUIRE(tbl_orders->GetSchema().GetColumn(0).GetName() == "order_id");
        REQUIRE(tbl_orders->GetSchema().GetColumn(1).GetName() == "total");

        REQUIRE(disk_mgr.Close().ok());
    }

    if (std::filesystem::exists(test_db)) {
        std::filesystem::remove(test_db);
    }
}
