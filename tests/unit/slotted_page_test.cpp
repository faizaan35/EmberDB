#include <catch2/catch.hpp>
#include "emberdb/storage/page/slotted_page.h"

TEST_CASE("SlottedPage record operations and free space management", "[storage][slotted_page]") {
    emberdb::Page raw_page;
    emberdb::SlottedPage page(&raw_page);
    page.Init(1);

    REQUIRE(page.GetPageId() == 1);
    REQUIRE(page.GetSlotCount() == 0);
    REQUIRE(page.GetFreeSpace() == emberdb::PAGE_SIZE - emberdb::PAGE_HEADER_SIZE);

    emberdb::Column c1("data", emberdb::TypeId::VARCHAR, 255);
    emberdb::Schema schema({c1});

    // Insert records
    emberdb::Record r1({emberdb::Value("First Record")}, schema);
    emberdb::Record r2({emberdb::Value("Second Record with extra text")}, schema);

    emberdb::RID rid1, rid2;
    REQUIRE(page.InsertRecord(r1, rid1));
    REQUIRE(page.InsertRecord(r2, rid2));

    REQUIRE(rid1.page_id == 1);
    REQUIRE(rid1.slot_id == 0);
    REQUIRE(rid2.page_id == 1);
    REQUIRE(rid2.slot_id == 1);
    REQUIRE(page.GetSlotCount() == 2);

    // Read records back
    emberdb::Record out1, out2;
    REQUIRE(page.GetRecord(rid1, out1));
    REQUIRE(page.GetRecord(rid2, out2));

    REQUIRE(out1.GetValue(schema, 0).GetAsVarChar() == "First Record");
    REQUIRE(out2.GetValue(schema, 0).GetAsVarChar() == "Second Record with extra text");

    // Update record
    emberdb::Record updated_r1({emberdb::Value("Updated First")}, schema);
    REQUIRE(page.UpdateRecord(rid1, updated_r1));

    emberdb::Record out_updated;
    REQUIRE(page.GetRecord(rid1, out_updated));
    REQUIRE(out_updated.GetValue(schema, 0).GetAsVarChar() == "Updated First");

    // Delete record
    REQUIRE(page.DeleteRecord(rid1));
    REQUIRE_FALSE(page.GetRecord(rid1, out1));

    // Slot 1 should still be accessible
    REQUIRE(page.GetRecord(rid2, out2));
    REQUIRE(out2.GetValue(schema, 0).GetAsVarChar() == "Second Record with extra text");

    // Reusing deleted slot
    emberdb::Record r3({emberdb::Value("Reused Slot Record")}, schema);
    emberdb::RID rid3;
    REQUIRE(page.InsertRecord(r3, rid3));
    REQUIRE(rid3.slot_id == 0); // Reused slot 0!
}
