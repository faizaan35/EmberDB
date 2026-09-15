#pragma once

#include <cstdint>
#include <cstddef>
#include <string_view>

namespace forgedb {

// Version constants
constexpr std::string_view FORGEDB_VERSION = "0.1.0";
constexpr int FORGEDB_VERSION_MAJOR = 0;
constexpr int FORGEDB_VERSION_MINOR = 1;
constexpr int FORGEDB_VERSION_PATCH = 0;

// Storage configuration per AGENTS.md #21
// Fixed page size of 4096 bytes
constexpr size_t PAGE_SIZE = 4096;

// Identifiers
using page_id_t = int32_t;
using slot_id_t = int32_t;
using txn_id_t = int64_t;
using lsn_t = int64_t;

// Sentinel values
constexpr page_id_t INVALID_PAGE_ID = -1;
constexpr slot_id_t INVALID_SLOT_ID = -1;
constexpr txn_id_t INVALID_TXN_ID = -1;
constexpr lsn_t INVALID_LSN = -1;

} // namespace forgedb
