#include <catch2/catch.hpp>
#include "emberdb/common/config.h"
#include "emberdb/common/status.h"
#include "emberdb/emberdb.h"

TEST_CASE("Configuration constants match AGENTS.md requirements", "[config]") {
    SECTION("Page size is exactly 4096 bytes per AGENTS.md section 21") {
        REQUIRE(emberdb::PAGE_SIZE == 4096);
    }

    SECTION("Sentinel values are well-defined") {
        REQUIRE(emberdb::INVALID_PAGE_ID == -1);
        REQUIRE(emberdb::INVALID_SLOT_ID == -1);
        REQUIRE(emberdb::INVALID_TXN_ID == -1);
        REQUIRE(emberdb::INVALID_LSN == -1);
    }

    SECTION("Version information is present") {
        REQUIRE_FALSE(emberdb::EMBERDB_VERSION.empty());
        REQUIRE(emberdb::EMBERDB_VERSION_MAJOR == 0);
        REQUIRE(emberdb::EMBERDB_VERSION_MINOR == 1);
        REQUIRE(emberdb::EMBERDB_VERSION_PATCH == 0);
    }
}

TEST_CASE("Status object correctly encodes error types and messages", "[status]") {
    SECTION("Status::OK represents successful operation") {
        emberdb::Status s = emberdb::Status::OK();
        REQUIRE(s.ok());
        REQUIRE(s.code() == emberdb::StatusCode::OK);
        REQUIRE(s.message().empty());
        REQUIRE(s.ToString() == "OK");
    }

    SECTION("Status error factories preserve code and message") {
        auto not_found = emberdb::Status::NotFound("table 'users' not found");
        REQUIRE_FALSE(not_found.ok());
        REQUIRE(not_found.code() == emberdb::StatusCode::NotFound);
        REQUIRE(not_found.message() == "table 'users' not found");
        REQUIRE(not_found.ToString() == "NotFound: table 'users' not found");

        auto io_err = emberdb::Status::IOError("disk full");
        REQUIRE_FALSE(io_err.ok());
        REQUIRE(io_err.code() == emberdb::StatusCode::IOError);
        REQUIRE(io_err.ToString() == "IOError: disk full");

        auto corrupt = emberdb::Status::Corruption("page checksum failure");
        REQUIRE_FALSE(corrupt.ok());
        REQUIRE(corrupt.code() == emberdb::StatusCode::Corruption);

        auto syntax = emberdb::Status::InvalidSyntax("unexpected token at position 12");
        REQUIRE_FALSE(syntax.ok());
        REQUIRE(syntax.code() == emberdb::StatusCode::InvalidSyntax);
    }

    SECTION("Status equality and inequality") {
        auto s1 = emberdb::Status::NotFound("item");
        auto s2 = emberdb::Status::NotFound("item");
        auto s3 = emberdb::Status::NotFound("other");
        auto s4 = emberdb::Status::IOError("item");

        REQUIRE(s1 == s2);
        REQUIRE(s1 != s3);
        REQUIRE(s1 != s4);
    }
}

TEST_CASE("Result template encapsulates value or failure status", "[result]") {
    SECTION("Result containing a value") {
        emberdb::Result<int> res(42);
        REQUIRE(res.ok());
        REQUIRE(*res == 42);
        REQUIRE(res.value() == 42);
        REQUIRE(res.status().ok());
    }

    SECTION("Result containing a failure Status") {
        emberdb::Result<int> res(emberdb::Status::NotFound("key not found"));
        REQUIRE_FALSE(res.ok());
        REQUIRE(res.status().code() == emberdb::StatusCode::NotFound);
        REQUIRE_THROWS_AS(res.value(), std::runtime_error);
    }

    SECTION("Result with complex type like std::string") {
        emberdb::Result<std::string> res(std::string("EmberDB"));
        REQUIRE(res.ok());
        REQUIRE(*res == "EmberDB");
        REQUIRE(res->length() == 7);
    }
}

TEST_CASE("EmberDBInstance basic lifecycle", "[engine]") {
    emberdb::EmberDBInstance db("data/test_db");
    REQUIRE_FALSE(db.IsOpen());
    REQUIRE(db.GetDbDirectory() == "data/test_db");

    auto open_status = db.Open();
    REQUIRE(open_status.ok());
    REQUIRE(db.IsOpen());

    auto close_status = db.Close();
    REQUIRE(close_status.ok());
    REQUIRE_FALSE(db.IsOpen());
}
