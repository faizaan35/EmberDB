#include <catch2/catch.hpp>
#include "forgedb/storage/record/record.h"

TEST_CASE("Record serialization, values, and nulls", "[record]") {
    forgedb::Column c1("id", forgedb::TypeId::INTEGER, false);
    forgedb::Column c2("name", forgedb::TypeId::VARCHAR, 100, false);
    forgedb::Column c3("score", forgedb::TypeId::DOUBLE, true);
    forgedb::Column c4("active", forgedb::TypeId::BOOLEAN, true);

    forgedb::Schema schema({c1, c2, c3, c4});

    std::vector<forgedb::Value> vals1 = {
        forgedb::Value(static_cast<int32_t>(101)),
        forgedb::Value("Alice Cooper"),
        forgedb::Value(95.5),
        forgedb::Value(true)
    };

    forgedb::Record rec1(vals1, schema);
    REQUIRE(rec1.GetLength() > 0);

    // Extract values
    REQUIRE(rec1.GetValue(schema, 0).GetAsInteger() == 101);
    REQUIRE(rec1.GetValue(schema, 1).GetAsVarChar() == "Alice Cooper");
    REQUIRE(rec1.GetValue(schema, 2).GetAsDouble() == Approx(95.5));
    REQUIRE(rec1.GetValue(schema, 3).GetAsBoolean() == true);

    // Record with NULLs
    std::vector<forgedb::Value> vals2 = {
        forgedb::Value(static_cast<int32_t>(102)),
        forgedb::Value("Bob Marley"),
        forgedb::Value::Null(forgedb::TypeId::DOUBLE),
        forgedb::Value::Null(forgedb::TypeId::BOOLEAN)
    };

    forgedb::Record rec2(vals2, schema);
    REQUIRE(rec2.GetValue(schema, 0).GetAsInteger() == 102);
    REQUIRE(rec2.GetValue(schema, 1).GetAsVarChar() == "Bob Marley");
    REQUIRE(rec2.GetValue(schema, 2).IsNull());
    REQUIRE(rec2.GetValue(schema, 3).IsNull());

    // Byte round-trip
    forgedb::Record rec_copy(rec1.GetData(), rec1.GetLength(), forgedb::RID(1, 0));
    REQUIRE(rec_copy.GetRID() == forgedb::RID(1, 0));
    REQUIRE(rec_copy.GetValue(schema, 0).GetAsInteger() == 101);
    REQUIRE(rec_copy.GetValue(schema, 1).GetAsVarChar() == "Alice Cooper");
}
