#include <catch2/catch.hpp>
#include "emberdb/emberdb.h"
#include "emberdb/storage/page/slotted_page.h"
#include <filesystem>
#include <vector>
#include <string>

using namespace emberdb;

namespace {
    const std::string REG_TEST_DIR = "data/test_correctness_regression";

    void CleanupRegDir() {
        std::error_code ec;
        std::filesystem::remove_all(REG_TEST_DIR, ec);
    }
}

TEST_CASE("Correctness Regression: Strict and Inclusive Index Range Scan Boundaries", "[regression][index]") {
    CleanupRegDir();

    {
        EmberDBInstance db(REG_TEST_DIR);
        REQUIRE(db.Open().ok());

        auto res = db.ExecuteQuery("CREATE TABLE numbers (id INT, label VARCHAR);");
        REQUIRE(res.success);

        auto idx_res = db.ExecuteQuery("CREATE INDEX idx_num ON numbers(id);");
        REQUIRE(idx_res.success);

        for (int i = 1; i <= 20; ++i) {
            std::string ins = "INSERT INTO numbers VALUES (" + std::to_string(i) + ", 'val" + std::to_string(i) + "');";
            REQUIRE(db.ExecuteQuery(ins).success);
        }

        // 1. Point lookup (=)
        {
            auto q = db.ExecuteQuery("SELECT * FROM numbers WHERE id = 10;");
            REQUIRE(q.success);
            REQUIRE(q.rows.size() == 1);
            REQUIRE(q.rows[0].GetValue(q.schema, 0).GetAsInteger() == 10);
        }

        // 2. Strict greater than (id > 10) - must NEVER return 10
        {
            auto q = db.ExecuteQuery("SELECT * FROM numbers WHERE id > 10 ORDER BY id;");
            REQUIRE(q.success);
            REQUIRE(q.rows.size() == 10); // 11 .. 20
            for (const auto& row : q.rows) {
                int val = row.GetValue(q.schema, 0).GetAsInteger();
                REQUIRE(val > 10);
            }
            REQUIRE(q.rows[0].GetValue(q.schema, 0).GetAsInteger() == 11);
        }

        // 3. Inclusive greater than or equal (id >= 10) - MUST return 10
        {
            auto q = db.ExecuteQuery("SELECT * FROM numbers WHERE id >= 10 ORDER BY id;");
            REQUIRE(q.success);
            REQUIRE(q.rows.size() == 11); // 10 .. 20
            REQUIRE(q.rows[0].GetValue(q.schema, 0).GetAsInteger() == 10);
        }

        // 4. Strict less than (id < 10) - must NEVER return 10
        {
            auto q = db.ExecuteQuery("SELECT * FROM numbers WHERE id < 10 ORDER BY id;");
            REQUIRE(q.success);
            REQUIRE(q.rows.size() == 9); // 1 .. 9
            for (const auto& row : q.rows) {
                int val = row.GetValue(q.schema, 0).GetAsInteger();
                REQUIRE(val < 10);
            }
            REQUIRE(q.rows.back().GetValue(q.schema, 0).GetAsInteger() == 9);
        }

        // 5. Inclusive less than or equal (id <= 10) - MUST return 10
        {
            auto q = db.ExecuteQuery("SELECT * FROM numbers WHERE id <= 10 ORDER BY id;");
            REQUIRE(q.success);
            REQUIRE(q.rows.size() == 10); // 1 .. 10
            REQUIRE(q.rows.back().GetValue(q.schema, 0).GetAsInteger() == 10);
        }

        // 6. Reversed orientation: 10 < id (equivalent to id > 10)
        {
            auto q = db.ExecuteQuery("SELECT * FROM numbers WHERE 10 < id ORDER BY id;");
            REQUIRE(q.success);
            REQUIRE(q.rows.size() == 10);
            REQUIRE(q.rows[0].GetValue(q.schema, 0).GetAsInteger() == 11);
        }

        // 7. Reversed orientation: 10 <= id (equivalent to id >= 10)
        {
            auto q = db.ExecuteQuery("SELECT * FROM numbers WHERE 10 <= id ORDER BY id;");
            REQUIRE(q.success);
            REQUIRE(q.rows.size() == 11);
            REQUIRE(q.rows[0].GetValue(q.schema, 0).GetAsInteger() == 10);
        }

        // 8. Reversed orientation: 10 > id (equivalent to id < 10)
        {
            auto q = db.ExecuteQuery("SELECT * FROM numbers WHERE 10 > id ORDER BY id;");
            REQUIRE(q.success);
            REQUIRE(q.rows.size() == 9);
            REQUIRE(q.rows.back().GetValue(q.schema, 0).GetAsInteger() == 9);
        }

        // 9. Reversed orientation: 10 >= id (equivalent to id <= 10)
        {
            auto q = db.ExecuteQuery("SELECT * FROM numbers WHERE 10 >= id ORDER BY id;");
            REQUIRE(q.success);
            REQUIRE(q.rows.size() == 10);
            REQUIRE(q.rows.back().GetValue(q.schema, 0).GetAsInteger() == 10);
        }

        // 10. Bounded range with strict boundaries on both sides (id > 10 AND id < 15)
        {
            auto q = db.ExecuteQuery("SELECT * FROM numbers WHERE id > 10 AND id < 15 ORDER BY id;");
            REQUIRE(q.success);
            REQUIRE(q.rows.size() == 4); // 11, 12, 13, 14
            REQUIRE(q.rows[0].GetValue(q.schema, 0).GetAsInteger() == 11);
            REQUIRE(q.rows.back().GetValue(q.schema, 0).GetAsInteger() == 14);
        }

        // 11. Bounded range with inclusive boundaries on both sides (id >= 10 AND id <= 15)
        {
            auto q = db.ExecuteQuery("SELECT * FROM numbers WHERE id >= 10 AND id <= 15 ORDER BY id;");
            REQUIRE(q.success);
            REQUIRE(q.rows.size() == 6); // 10, 11, 12, 13, 14, 15
            REQUIRE(q.rows[0].GetValue(q.schema, 0).GetAsInteger() == 10);
            REQUIRE(q.rows.back().GetValue(q.schema, 0).GetAsInteger() == 15);
        }

        // 12. Bounded range with mixed boundaries (id > 10 AND id <= 15)
        {
            auto q = db.ExecuteQuery("SELECT * FROM numbers WHERE id > 10 AND id <= 15 ORDER BY id;");
            REQUIRE(q.success);
            REQUIRE(q.rows.size() == 5); // 11, 12, 13, 14, 15
            REQUIRE(q.rows[0].GetValue(q.schema, 0).GetAsInteger() == 11);
            REQUIRE(q.rows.back().GetValue(q.schema, 0).GetAsInteger() == 15);
        }

        // 13. Bounded range with mixed boundaries (id >= 10 AND id < 15)
        {
            auto q = db.ExecuteQuery("SELECT * FROM numbers WHERE id >= 10 AND id < 15 ORDER BY id;");
            REQUIRE(q.success);
            REQUIRE(q.rows.size() == 5); // 10, 11, 12, 13, 14
            REQUIRE(q.rows[0].GetValue(q.schema, 0).GetAsInteger() == 10);
            REQUIRE(q.rows.back().GetValue(q.schema, 0).GetAsInteger() == 14);
        }

        REQUIRE(db.Close().ok());
    }

    CleanupRegDir();
}

TEST_CASE("Correctness Regression: Index Range Scans on Multiple Data Types", "[regression][types]") {
    CleanupRegDir();

    {
        EmberDBInstance db(REG_TEST_DIR);
        REQUIRE(db.Open().ok());

        // --- BIGINT test (> 2^31-1 values) ---
        REQUIRE(db.ExecuteQuery("CREATE TABLE big_items (val BIGINT, name VARCHAR);").success);
        REQUIRE(db.ExecuteQuery("CREATE INDEX idx_big ON big_items(val);").success);

        REQUIRE(db.ExecuteQuery("INSERT INTO big_items VALUES (1000, 'small');").success);
        REQUIRE(db.ExecuteQuery("INSERT INTO big_items VALUES (3000000000, 'three_billion');").success);
        REQUIRE(db.ExecuteQuery("INSERT INTO big_items VALUES (5000000000, 'five_billion');").success);
        REQUIRE(db.ExecuteQuery("INSERT INTO big_items VALUES (7000000000, 'seven_billion');").success);

        // Strict > 3B
        {
            auto q = db.ExecuteQuery("SELECT * FROM big_items WHERE val > 3000000000 ORDER BY val;");
            REQUIRE(q.success);
            REQUIRE(q.rows.size() == 2); // 5B, 7B
            REQUIRE(q.rows[0].GetValue(q.schema, 0).GetAsBigInt() == 5000000000LL);
            REQUIRE(q.rows[1].GetValue(q.schema, 0).GetAsBigInt() == 7000000000LL);
        }

        // Inclusive >= 3B
        {
            auto q = db.ExecuteQuery("SELECT * FROM big_items WHERE val >= 3000000000 ORDER BY val;");
            REQUIRE(q.success);
            REQUIRE(q.rows.size() == 3); // 3B, 5B, 7B
            REQUIRE(q.rows[0].GetValue(q.schema, 0).GetAsBigInt() == 3000000000LL);
        }

        // Strict < 5B
        {
            auto q = db.ExecuteQuery("SELECT * FROM big_items WHERE val < 5000000000 ORDER BY val;");
            REQUIRE(q.success);
            REQUIRE(q.rows.size() == 2); // 1000, 3B
            REQUIRE(q.rows.back().GetValue(q.schema, 0).GetAsBigInt() == 3000000000LL);
        }

        // Inclusive <= 5B
        {
            auto q = db.ExecuteQuery("SELECT * FROM big_items WHERE val <= 5000000000 ORDER BY val;");
            REQUIRE(q.success);
            REQUIRE(q.rows.size() == 3); // 1000, 3B, 5B
            REQUIRE(q.rows.back().GetValue(q.schema, 0).GetAsBigInt() == 5000000000LL);
        }

        // --- DOUBLE test ---
        REQUIRE(db.ExecuteQuery("CREATE TABLE measurements (temp DOUBLE, city VARCHAR);").success);
        REQUIRE(db.ExecuteQuery("CREATE INDEX idx_temp ON measurements(temp);").success);

        REQUIRE(db.ExecuteQuery("INSERT INTO measurements VALUES (-40.5, 'Fairbanks');").success);
        REQUIRE(db.ExecuteQuery("INSERT INTO measurements VALUES (-10.0, 'Moscow');").success);
        REQUIRE(db.ExecuteQuery("INSERT INTO measurements VALUES (0.0, 'Reykjavik');").success);
        REQUIRE(db.ExecuteQuery("INSERT INTO measurements VALUES (25.5, 'Rome');").success);
        REQUIRE(db.ExecuteQuery("INSERT INTO measurements VALUES (37.2, 'Cairo');").success);
        REQUIRE(db.ExecuteQuery("INSERT INTO measurements VALUES (100.0, 'Boiling');").success);

        // Strict > 25.5
        {
            auto q = db.ExecuteQuery("SELECT * FROM measurements WHERE temp > 25.5 ORDER BY temp;");
            REQUIRE(q.success);
            REQUIRE(q.rows.size() == 2); // 37.2, 100.0
            REQUIRE(q.rows[0].GetValue(q.schema, 0).GetAsDouble() == Approx(37.2));
        }

        // Inclusive >= 25.5
        {
            auto q = db.ExecuteQuery("SELECT * FROM measurements WHERE temp >= 25.5 ORDER BY temp;");
            REQUIRE(q.success);
            REQUIRE(q.rows.size() == 3); // 25.5, 37.2, 100.0
            REQUIRE(q.rows[0].GetValue(q.schema, 0).GetAsDouble() == Approx(25.5));
        }

        // Strict < 0.0
        {
            auto q = db.ExecuteQuery("SELECT * FROM measurements WHERE temp < 0.0 ORDER BY temp;");
            REQUIRE(q.success);
            REQUIRE(q.rows.size() == 2); // -40.5, -10.0
            REQUIRE(q.rows.back().GetValue(q.schema, 0).GetAsDouble() == Approx(-10.0));
        }

        // Inclusive <= 0.0
        {
            auto q = db.ExecuteQuery("SELECT * FROM measurements WHERE temp <= 0.0 ORDER BY temp;");
            REQUIRE(q.success);
            REQUIRE(q.rows.size() == 3); // -40.5, -10.0, 0.0
            REQUIRE(q.rows.back().GetValue(q.schema, 0).GetAsDouble() == Approx(0.0));
        }

        // --- VARCHAR test ---
        REQUIRE(db.ExecuteQuery("CREATE TABLE lexicon (word VARCHAR, score INT);").success);
        REQUIRE(db.ExecuteQuery("CREATE INDEX idx_word ON lexicon(word);").success);

        REQUIRE(db.ExecuteQuery("INSERT INTO lexicon VALUES ('apple', 1);").success);
        REQUIRE(db.ExecuteQuery("INSERT INTO lexicon VALUES ('banana', 2);").success);
        REQUIRE(db.ExecuteQuery("INSERT INTO lexicon VALUES ('cherry', 3);").success);
        REQUIRE(db.ExecuteQuery("INSERT INTO lexicon VALUES ('date', 4);").success);
        REQUIRE(db.ExecuteQuery("INSERT INTO lexicon VALUES ('fig', 5);").success);
        REQUIRE(db.ExecuteQuery("INSERT INTO lexicon VALUES ('grape', 6);").success);

        // Strict > 'cherry'
        {
            auto q = db.ExecuteQuery("SELECT * FROM lexicon WHERE word > 'cherry' ORDER BY word;");
            REQUIRE(q.success);
            REQUIRE(q.rows.size() == 3); // date, fig, grape
            REQUIRE(q.rows[0].GetValue(q.schema, 0).GetAsVarChar() == "date");
        }

        // Inclusive >= 'cherry'
        {
            auto q = db.ExecuteQuery("SELECT * FROM lexicon WHERE word >= 'cherry' ORDER BY word;");
            REQUIRE(q.success);
            REQUIRE(q.rows.size() == 4); // cherry, date, fig, grape
            REQUIRE(q.rows[0].GetValue(q.schema, 0).GetAsVarChar() == "cherry");
        }

        // Strict < 'cherry'
        {
            auto q = db.ExecuteQuery("SELECT * FROM lexicon WHERE word < 'cherry' ORDER BY word;");
            REQUIRE(q.success);
            REQUIRE(q.rows.size() == 2); // apple, banana
            REQUIRE(q.rows.back().GetValue(q.schema, 0).GetAsVarChar() == "banana");
        }

        // Inclusive <= 'cherry'
        {
            auto q = db.ExecuteQuery("SELECT * FROM lexicon WHERE word <= 'cherry' ORDER BY word;");
            REQUIRE(q.success);
            REQUIRE(q.rows.size() == 3); // apple, banana, cherry
            REQUIRE(q.rows.back().GetValue(q.schema, 0).GetAsVarChar() == "cherry");
        }

        // Bounded range on VARCHAR
        {
            auto q = db.ExecuteQuery("SELECT * FROM lexicon WHERE word >= 'banana' AND word < 'fig' ORDER BY word;");
            REQUIRE(q.success);
            REQUIRE(q.rows.size() == 3); // banana, cherry, date
            REQUIRE(q.rows[0].GetValue(q.schema, 0).GetAsVarChar() == "banana");
            REQUIRE(q.rows.back().GetValue(q.schema, 0).GetAsVarChar() == "date");
        }

        REQUIRE(db.Close().ok());
    }

    CleanupRegDir();
}

TEST_CASE("Correctness Regression: SlottedPage::RedoInsert Intermediate Slot Initialization", "[regression][slotted_page]") {
    char page_data[PAGE_SIZE];
    std::memset(page_data, 0xAA, PAGE_SIZE); // Fill with non-zero garbage to test proper initialization

    SlottedPage sp(page_data);
    sp.Init(100);
    REQUIRE(sp.GetSlotCount() == 0);

    // Create a record
    Schema schema({Column("id", TypeId::INTEGER), Column("text", TypeId::VARCHAR, 32)});
    Record rec({Value(42), Value("hello")}, schema);

    // Redo insert at slot 5 on a page with slot_count == 0
    RID rid(100, 5);
    bool ok = sp.RedoInsert(rid, rec);
    REQUIRE(ok);
    REQUIRE(sp.GetSlotCount() == 6);

    // Intermediate slots 0..4 must be safely initialized as tombstones (size == 0)
    for (slot_id_t i = 0; i < 5; ++i) {
        Slot s = sp.GetSlot(i);
        REQUIRE(s.size == 0);
        Record dummy;
        REQUIRE_FALSE(sp.GetRecord(RID(100, i), dummy));
    }

    // Slot 5 must contain the record
    Slot s5 = sp.GetSlot(5);
    REQUIRE(s5.size > 0);
    Record fetched;
    REQUIRE(sp.GetRecord(RID(100, 5), fetched));
    REQUIRE(fetched.GetValue(schema, 0).GetAsInteger() == 42);
    REQUIRE(fetched.GetValue(schema, 1).GetAsVarChar() == "hello");
}

TEST_CASE("Correctness Regression: DROP TABLE Removes Table and Index Metadata and Persists", "[regression][drop_table]") {
    CleanupRegDir();

    {
        EmberDBInstance db(REG_TEST_DIR);
        REQUIRE(db.Open().ok());

        // Create table and insert records
        REQUIRE(db.ExecuteQuery("CREATE TABLE accounts (id INT, balance DOUBLE);").success);
        REQUIRE(db.ExecuteQuery("INSERT INTO accounts VALUES (1, 100.5), (2, 200.0);").success);

        // Create secondary index
        REQUIRE(db.ExecuteQuery("CREATE INDEX idx_acc_id ON accounts(id);").success);

        REQUIRE(db.GetCatalog()->HasTable("accounts"));
        REQUIRE(db.GetCatalog()->HasIndex("idx_acc_id"));

        // Query table
        auto sel = db.ExecuteQuery("SELECT * FROM accounts;");
        REQUIRE(sel.success);
        REQUIRE(sel.rows.size() == 2);

        // DROP TABLE
        auto drop_res = db.ExecuteQuery("DROP TABLE accounts;");
        REQUIRE(drop_res.success);

        // Verify table and index are removed from catalog
        REQUIRE_FALSE(db.GetCatalog()->HasTable("accounts"));
        REQUIRE_FALSE(db.GetCatalog()->HasIndex("idx_acc_id"));

        // Querying dropped table must fail with "Table not found"
        auto sel_after = db.ExecuteQuery("SELECT * FROM accounts;");
        REQUIRE_FALSE(sel_after.success);
        REQUIRE(sel_after.error_message.find("not found") != std::string::npos);

        // INSERT into dropped table must fail
        auto ins_after = db.ExecuteQuery("INSERT INTO accounts VALUES (3, 300.0);");
        REQUIRE_FALSE(ins_after.success);
        REQUIRE(ins_after.error_message.find("not found") != std::string::npos);

        // UPDATE on dropped table must fail
        auto upd_after = db.ExecuteQuery("UPDATE accounts SET balance = 50.0;");
        REQUIRE_FALSE(upd_after.success);
        REQUIRE(upd_after.error_message.find("not found") != std::string::npos);

        // DELETE on dropped table must fail
        auto del_after = db.ExecuteQuery("DELETE FROM accounts;");
        REQUIRE_FALSE(del_after.success);
        REQUIRE(del_after.error_message.find("not found") != std::string::npos);

        // Close database cleanly
        REQUIRE(db.Close().ok());
    }

    // Reopen database from disk: verify DROP TABLE persisted across restart
    {
        EmberDBInstance db(REG_TEST_DIR);
        REQUIRE(db.Open().ok());

        REQUIRE_FALSE(db.GetCatalog()->HasTable("accounts"));
        REQUIRE_FALSE(db.GetCatalog()->HasIndex("idx_acc_id"));

        auto sel_reopen = db.ExecuteQuery("SELECT * FROM accounts;");
        REQUIRE_FALSE(sel_reopen.success);
        REQUIRE(sel_reopen.error_message.find("not found") != std::string::npos);

        REQUIRE(db.Close().ok());
    }

    CleanupRegDir();
}
