#include <catch2/catch.hpp>
#include "forgedb/common/config.h"
#include "forgedb/common/status.h"
#include "forgedb/forgedb.h"

TEST_CASE("Configuration constants match AGENTS.md requirements", "[config]") {
    SECTION("Page size is exactly 4096 bytes per AGENTS.md section 21") {
        REQUIRE(forgedb::PAGE_SIZE == 4096);
    }

    SECTION("Sentinel values are well-defined") {
        REQUIRE(forgedb::INVALID_PAGE_ID == -1);
        REQUIRE(forgedb::INVALID_SLOT_ID == -1);
        REQUIRE(forgedb::INVALID_TXN_ID == -1);
        REQUIRE(forgedb::INVALID_LSN == -1);
    }

    SECTION("Version information is present") {
        REQUIRE_FALSE(forgedb::FORGEDB_VERSION.empty());
        REQUIRE(forgedb::FORGEDB_VERSION_MAJOR == 0);
        REQUIRE(forgedb::FORGEDB_VERSION_MINOR == 1);
        REQUIRE(forgedb::FORGEDB_VERSION_PATCH == 0);
    }
}

TEST_CASE("Status object correctly encodes error types and messages", "[status]") {
    SECTION("Status::OK represents successful operation") {
        forgedb::Status s = forgedb::Status::OK();
        REQUIRE(s.ok());
        REQUIRE(s.code() == forgedb::StatusCode::OK);
        REQUIRE(s.message().empty());
        REQUIRE(s.ToString() == "OK");
    }

    SECTION("Status error factories preserve code and message") {
        auto not_found = forgedb::Status::NotFound("table 'users' not found");
        REQUIRE_FALSE(not_found.ok());
        REQUIRE(not_found.code() == forgedb::StatusCode::NotFound);
        REQUIRE(not_found.message() == "table 'users' not found");
        REQUIRE(not_found.ToString() == "NotFound: table 'users' not found");

        auto io_err = forgedb::Status::IOError("disk full");
        REQUIRE_FALSE(io_err.ok());
        REQUIRE(io_err.code() == forgedb::StatusCode::IOError);
        REQUIRE(io_err.ToString() == "IOError: disk full");

        auto corrupt = forgedb::Status::Corruption("page checksum failure");
        REQUIRE_FALSE(corrupt.ok());
        REQUIRE(corrupt.code() == forgedb::StatusCode::Corruption);

        auto syntax = forgedb::Status::InvalidSyntax("unexpected token at position 12");
        REQUIRE_FALSE(syntax.ok());
        REQUIRE(syntax.code() == forgedb::StatusCode::InvalidSyntax);
    }

    SECTION("Status equality and inequality") {
        auto s1 = forgedb::Status::NotFound("item");
        auto s2 = forgedb::Status::NotFound("item");
        auto s3 = forgedb::Status::NotFound("other");
        auto s4 = forgedb::Status::IOError("item");

        REQUIRE(s1 == s2);
        REQUIRE(s1 != s3);
        REQUIRE(s1 != s4);
    }
}

TEST_CASE("Result template encapsulates value or failure status", "[result]") {
    SECTION("Result containing a value") {
        forgedb::Result<int> res(42);
        REQUIRE(res.ok());
        REQUIRE(*res == 42);
        REQUIRE(res.value() == 42);
        REQUIRE(res.status().ok());
    }

    SECTION("Result containing a failure Status") {
        forgedb::Result<int> res(forgedb::Status::NotFound("key not found"));
        REQUIRE_FALSE(res.ok());
        REQUIRE(res.status().code() == forgedb::StatusCode::NotFound);
        REQUIRE_THROWS_AS(res.value(), std::runtime_error);
    }

    SECTION("Result with complex type like std::string") {
        forgedb::Result<std::string> res(std::string("ForgeDB"));
        REQUIRE(res.ok());
        REQUIRE(*res == "ForgeDB");
        REQUIRE(res->length() == 7);
    }
}

TEST_CASE("ForgeDBInstance basic lifecycle", "[engine]") {
    forgedb::ForgeDBInstance db("data/test_db");
    REQUIRE_FALSE(db.IsOpen());
    REQUIRE(db.GetDbDirectory() == "data/test_db");

    auto open_status = db.Open();
    REQUIRE(open_status.ok());
    REQUIRE(db.IsOpen());

    auto close_status = db.Close();
    REQUIRE(close_status.ok());
    REQUIRE_FALSE(db.IsOpen());
}
