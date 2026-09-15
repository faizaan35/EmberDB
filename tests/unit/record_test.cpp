#include <catch2/catch.hpp>
#include "emberdb/storage/record/record.h"

TEST_CASE("Record serialization, values, and nulls", "[record]") {
    emberdb::Column c1("id", emberdb::TypeId::INTEGER, false);
    emberdb::Column c2("name", emberdb::TypeId::VARCHAR, 100, false);
    emberdb::Column c3("score", emberdb::TypeId::DOUBLE, true);
    emberdb::Column c4("active", emberdb::TypeId::BOOLEAN, true);

    emberdb::Schema schema({c1, c2, c3, c4});

    std::vector<emberdb::Value> vals1 = {
        emberdb::Value(static_cast<int32_t>(101)),
        emberdb::Value("Alice Cooper"),
        emberdb::Value(95.5),
        emberdb::Value(true)
    };

    emberdb::Record rec1(vals1, schema);
    REQUIRE(rec1.GetLength() > 0);

    // Extract values
    REQUIRE(rec1.GetValue(schema, 0).GetAsInteger() == 101);
    REQUIRE(rec1.GetValue(schema, 1).GetAsVarChar() == "Alice Cooper");
    REQUIRE(rec1.GetValue(schema, 2).GetAsDouble() == Approx(95.5));
    REQUIRE(rec1.GetValue(schema, 3).GetAsBoolean() == true);

    // Record with NULLs
    std::vector<emberdb::Value> vals2 = {
        emberdb::Value(static_cast<int32_t>(102)),
        emberdb::Value("Bob Marley"),
        emberdb::Value::Null(emberdb::TypeId::DOUBLE),
        emberdb::Value::Null(emberdb::TypeId::BOOLEAN)
    };

    emberdb::Record rec2(vals2, schema);
    REQUIRE(rec2.GetValue(schema, 0).GetAsInteger() == 102);
    REQUIRE(rec2.GetValue(schema, 1).GetAsVarChar() == "Bob Marley");
    REQUIRE(rec2.GetValue(schema, 2).IsNull());
    REQUIRE(rec2.GetValue(schema, 3).IsNull());

    // Byte round-trip
    emberdb::Record rec_copy(rec1.GetData(), rec1.GetLength(), emberdb::RID(1, 0));
    REQUIRE(rec_copy.GetRID() == emberdb::RID(1, 0));
    REQUIRE(rec_copy.GetValue(schema, 0).GetAsInteger() == 101);
    REQUIRE(rec_copy.GetValue(schema, 1).GetAsVarChar() == "Alice Cooper");
}
