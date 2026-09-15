#include <catch2/catch.hpp>
#include "forgedb/common/types.h"
#include "forgedb/catalog/column.h"
#include "forgedb/catalog/schema.h"
#include "forgedb/storage/page/page.h"

TEST_CASE("RID properties and comparisons", "[types]") {
    forgedb::RID invalid_rid;
    REQUIRE_FALSE(invalid_rid.IsValid());

    forgedb::RID rid1(10, 5);
    REQUIRE(rid1.IsValid());
    REQUIRE(rid1.page_id == 10);
    REQUIRE(rid1.slot_id == 5);
    REQUIRE(rid1.ToString() == "RID(10, 5)");

    forgedb::RID rid2(10, 5);
    forgedb::RID rid3(10, 6);
    forgedb::RID rid4(11, 1);

    REQUIRE(rid1 == rid2);
    REQUIRE(rid1 != rid3);
    REQUIRE(rid1 < rid3);
    REQUIRE(rid3 < rid4);
}

TEST_CASE("Value types, nulls, and conversions", "[types]") {
    SECTION("Boolean Value") {
        forgedb::Value v_true(true);
        forgedb::Value v_false(false);
        REQUIRE_FALSE(v_true.IsNull());
        REQUIRE(v_true.GetTypeId() == forgedb::TypeId::BOOLEAN);
        REQUIRE(v_true.GetAsBoolean() == true);
        REQUIRE(v_false.GetAsBoolean() == false);
        REQUIRE(v_true.ToString() == "true");
        REQUIRE(v_false.ToString() == "false");
    }

    SECTION("Integer and BigInt Values") {
        forgedb::Value v_int(static_cast<int32_t>(12345));
        REQUIRE(v_int.GetTypeId() == forgedb::TypeId::INTEGER);
        REQUIRE(v_int.GetAsInteger() == 12345);
        REQUIRE(v_int.GetAsBigInt() == 12345);
        REQUIRE(v_int.GetAsDouble() == 12345.0);
        REQUIRE(v_int.ToString() == "12345");

        forgedb::Value v_big(static_cast<int64_t>(9876543210LL));
        REQUIRE(v_big.GetTypeId() == forgedb::TypeId::BIGINT);
        REQUIRE(v_big.GetAsBigInt() == 9876543210LL);
        REQUIRE(v_big.ToString() == "9876543210");
    }

    SECTION("Double Value") {
        forgedb::Value v_double(3.14159);
        REQUIRE(v_double.GetTypeId() == forgedb::TypeId::DOUBLE);
        REQUIRE(v_double.GetAsDouble() == Approx(3.14159));
    }

    SECTION("VarChar Value") {
        forgedb::Value v_str("ForgeDB Engine");
        REQUIRE(v_str.GetTypeId() == forgedb::TypeId::VARCHAR);
        REQUIRE(v_str.GetAsVarChar() == "ForgeDB Engine");
        REQUIRE(v_str.ToString() == "ForgeDB Engine");
    }

    SECTION("Null Value") {
        auto v_null = forgedb::Value::Null(forgedb::TypeId::INTEGER);
        REQUIRE(v_null.IsNull());
        REQUIRE(v_null.GetTypeId() == forgedb::TypeId::INTEGER);
        REQUIRE(v_null.ToString() == "NULL");
        REQUIRE_THROWS_AS(v_null.GetAsInteger(), std::runtime_error);
    }
}

TEST_CASE("Value binary serialization and deserialization", "[types]") {
    std::vector<forgedb::Value> test_values = {
        forgedb::Value(true),
        forgedb::Value(false),
        forgedb::Value(static_cast<int32_t>(-42)),
        forgedb::Value(static_cast<int64_t>(1234567890123LL)),
        forgedb::Value(2.718281828),
        forgedb::Value("A variable length string test for ForgeDB"),
        forgedb::Value::Null(forgedb::TypeId::VARCHAR),
        forgedb::Value::Null(forgedb::TypeId::INTEGER)
    };

    char buffer[512];
    for (const auto& original : test_values) {
        size_t expected_size = original.GetSerializedSize();
        original.SerializeTo(buffer);

        size_t bytes_read = 0;
        forgedb::Value deserialized = forgedb::Value::DeserializeFrom(buffer, original.GetTypeId(), bytes_read);

        REQUIRE(bytes_read == expected_size);
        REQUIRE(deserialized.IsNull() == original.IsNull());
        REQUIRE(deserialized.GetTypeId() == original.GetTypeId());
        REQUIRE(deserialized == original);
    }
}

TEST_CASE("Column and Schema definitions and serialization", "[catalog]") {
    forgedb::Column c1("id", forgedb::TypeId::INTEGER, false);
    forgedb::Column c2("name", forgedb::TypeId::VARCHAR, 50, false);
    forgedb::Column c3("balance", forgedb::TypeId::DOUBLE, true);

    REQUIRE(c1.GetName() == "id");
    REQUIRE(c1.GetType() == forgedb::TypeId::INTEGER);
    REQUIRE_FALSE(c1.IsNullable());

    forgedb::Schema schema({c1, c2, c3});
    REQUIRE(schema.GetColumnCount() == 3);
    REQUIRE(schema.HasColumn("id"));
    REQUIRE(schema.HasColumn("name"));
    REQUIRE(schema.HasColumn("balance"));
    REQUIRE_FALSE(schema.HasColumn("non_existent"));

    REQUIRE(schema.GetColIdx("id") == 0);
    REQUIRE(schema.GetColIdx("name") == 1);
    REQUIRE(schema.GetColIdx("balance") == 2);

    // Test serialization
    std::vector<char> buffer(schema.GetSerializedSize());
    schema.SerializeTo(buffer.data());

    size_t bytes_read = 0;
    forgedb::Schema restored_schema = forgedb::Schema::DeserializeFrom(buffer.data(), bytes_read);

    REQUIRE(bytes_read == buffer.size());
    REQUIRE(restored_schema == schema);
    REQUIRE(restored_schema.GetColumnCount() == 3);
    REQUIRE(restored_schema.GetColumn(1).GetName() == "name");
}

TEST_CASE("Page in-memory structure and reset", "[storage]") {
    forgedb::Page page;
    REQUIRE(page.GetPageId() == forgedb::INVALID_PAGE_ID);
    REQUIRE(page.GetPinCount() == 0);
    REQUIRE_FALSE(page.IsDirty());

    page.SetPageId(42);
    page.SetLSN(1001);
    page.IncrementPinCount();
    page.SetDirty(true);

    REQUIRE(page.GetPageId() == 42);
    REQUIRE(page.GetLSN() == 1001);
    REQUIRE(page.GetPinCount() == 1);
    REQUIRE(page.IsDirty());

    page.DecrementPinCount();
    REQUIRE(page.GetPinCount() == 0);

    page.ResetMemory();
    REQUIRE(page.GetPageId() == forgedb::INVALID_PAGE_ID);
    REQUIRE(page.GetPinCount() == 0);
    REQUIRE_FALSE(page.IsDirty());
}
