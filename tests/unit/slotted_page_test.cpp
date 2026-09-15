#include <catch2/catch.hpp>
#include "forgedb/storage/page/slotted_page.h"

TEST_CASE("SlottedPage record operations and free space management", "[storage][slotted_page]") {
    forgedb::Page raw_page;
    forgedb::SlottedPage page(&raw_page);
    page.Init(1);

    REQUIRE(page.GetPageId() == 1);
    REQUIRE(page.GetSlotCount() == 0);
    REQUIRE(page.GetFreeSpace() == forgedb::PAGE_SIZE - forgedb::PAGE_HEADER_SIZE);

    forgedb::Column c1("data", forgedb::TypeId::VARCHAR, 255);
    forgedb::Schema schema({c1});

    // Insert records
    forgedb::Record r1({forgedb::Value("First Record")}, schema);
    forgedb::Record r2({forgedb::Value("Second Record with extra text")}, schema);

    forgedb::RID rid1, rid2;
    REQUIRE(page.InsertRecord(r1, rid1));
    REQUIRE(page.InsertRecord(r2, rid2));

    REQUIRE(rid1.page_id == 1);
    REQUIRE(rid1.slot_id == 0);
    REQUIRE(rid2.page_id == 1);
    REQUIRE(rid2.slot_id == 1);
    REQUIRE(page.GetSlotCount() == 2);

    // Read records back
    forgedb::Record out1, out2;
    REQUIRE(page.GetRecord(rid1, out1));
    REQUIRE(page.GetRecord(rid2, out2));

    REQUIRE(out1.GetValue(schema, 0).GetAsVarChar() == "First Record");
    REQUIRE(out2.GetValue(schema, 0).GetAsVarChar() == "Second Record with extra text");

    // Update record
    forgedb::Record updated_r1({forgedb::Value("Updated First")}, schema);
    REQUIRE(page.UpdateRecord(rid1, updated_r1));

    forgedb::Record out_updated;
    REQUIRE(page.GetRecord(rid1, out_updated));
    REQUIRE(out_updated.GetValue(schema, 0).GetAsVarChar() == "Updated First");

    // Delete record
    REQUIRE(page.DeleteRecord(rid1));
    REQUIRE_FALSE(page.GetRecord(rid1, out1));

    // Slot 1 should still be accessible
    REQUIRE(page.GetRecord(rid2, out2));
    REQUIRE(out2.GetValue(schema, 0).GetAsVarChar() == "Second Record with extra text");

    // Reusing deleted slot
    forgedb::Record r3({forgedb::Value("Reused Slot Record")}, schema);
    forgedb::RID rid3;
    REQUIRE(page.InsertRecord(r3, rid3));
    REQUIRE(rid3.slot_id == 0); // Reused slot 0!
}
