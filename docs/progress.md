# EmberDB — Implementation Progress Tracker

This document tracks progress across all implementation phases of **EmberDB** as specified in `AGENTS.md`.

---

## Phase Matrix

| Phase | Description | Status | Gate Status |
|---|---|---|---|
| **Phase 0** | Project Foundation (C++17, CMake, Catch2, Executables, Docs) | **COMPLETE** | **PASSED** |
| **Phase 1** | Basic Types + Pages + Disk Manager | **COMPLETE** | **PASSED** |
| **Phase 2** | Records + Tables + Catalog | **COMPLETE** | **PASSED** |
| **Phase 3** | SQL Lexer + Parser | **COMPLETE** | **PASSED** |
| **Phase 4** | Basic Query Execution | **COMPLETE** | **PASSED** |
| **Phase 5** | Query Features (ORDER BY, LIMIT, Aggregates, GROUP BY) | **COMPLETE** | **PASSED** |
| **Phase 6** | JOINs (Nested Loop Join, INNER / LEFT) | **COMPLETE** | **PASSED** |
| **Phase 7** | Buffer Pool (LRU, Pin/Unpin, Dirty Tracking) | **COMPLETE** | **PASSED** |
| **Phase 8** | B+ Tree Index (Search, Insert, Split, Range Scan) | **COMPLETE** | **PASSED** |
| **Phase 9** | Query Planner + Index Scan | **COMPLETE** | **PASSED** |
| **Phase 10** | Transactions (BEGIN, COMMIT, ROLLBACK) | **COMPLETE** | **PASSED** |
| **Phase 11** | Concurrency (Synchronization, Thread Safety) | **COMPLETE** | **PASSED** |
| **Phase 12** | Write-Ahead Logging (WAL, LSN, Log Records) | **COMPLETE** | **PASSED** |
| **Phase 13** | Crash Recovery (Redo, Undo, Checkpointing) | **COMPLETE** | **PASSED** |
| **Phase 14** | CLI Polish (Interactive REPL, Meta Commands) | **COMPLETE** | **PASSED** |
| **Phase 15** | HTTP API (C++ REST endpoints) | PENDING | NOT STARTED |
| **Phase 16** | React Web Interface (SQL Console) | PENDING | NOT STARTED |
| **Phase 17** | Integration + Final Hardening | PENDING | NOT STARTED |
| **Phase 18** | Documentation + Interview Readiness | PENDING | NOT STARTED |

---

## Detailed Phase Records

### Phase 0 — Project Foundation
* **Status**: **COMPLETE**
* **Completion Gate**: **PASSED** (CMake configure, build, all tests pass, standalone executable verified)

### Phase 1 — Basic Types + Pages + Disk Manager
* **Status**: **COMPLETE**
* **Completion Gate**: **PASSED** (Bit-for-bit page persistence across restart verified)

### Phase 2 — Records + Tables + Catalog
* **Status**: **COMPLETE**
* **Completion Gate**: **PASSED** (Table creation, record insertion, restart persistence verified)

### Phase 3 — SQL Lexer + Parser
* **Status**: **COMPLETE**
* **Completion Gate**: **PASSED** (Full statement AST parsing and error diagnostics verified)

### Phase 4 — Basic Query Execution
* **Status**: **COMPLETE**
* **Completion Gate**: **PASSED**
  * Exact CLI gate script tested and verified end-to-end:
    - `CREATE TABLE users (id INT, name VARCHAR, age INT);`
    - `INSERT INTO users VALUES (1, 'Faizaan', 23);`
    - `SELECT * FROM users;` -> verified row `(1, 'Faizaan', 23)`
    - `SELECT name FROM users WHERE age > 20;` -> verified row `('Faizaan')`
    - `UPDATE users SET age = 24 WHERE id = 1;` -> 1 row updated, verified `age == 24`
    - `DELETE FROM users WHERE id = 1;` -> 1 row deleted, verified 0 rows remaining
  * All previous tests preserved and passed (1179 assertions in 20 test cases).
* **What was Implemented**:
  * `ExpressionEvaluator`: Evaluates AST expressions (literals, column refs, arithmetic, comparisons, logical AND/OR/NOT).
  * Volcano iterator architecture: `AbstractExecutor`, `SeqScanExecutor`, `ProjectionExecutor`.
  * `ExecutionEngine`: Dispatches statements to executors and formats ASCII result tables.
  * Interactive CLI: Integrated execution engine, supporting multi-line SQL input and schema introspection meta-commands (`.tables`, `.schema`).
* **Build Command**: `cmake --build build --config Debug`
* **Test Command**: `ctest --test-dir build --output-on-failure` & `.\build\bin\emberdb_tests.exe`
* **Test Results**: 20 test cases, 1179 assertions passed, 0 failures.

### Phase 5 — Query Features (ORDER BY, LIMIT, Aggregates, GROUP BY)
* **Status**: **COMPLETE**
* **Completion Gate**: **PASSED**
  * Gate query: `SELECT age, COUNT(*) FROM users GROUP BY age ORDER BY age DESC LIMIT 5;` verified and tested end-to-end.
  * Verified correct aggregation, sorting, grouping, and limit pagination.
  * All 23 test cases (1272 assertions) passed.
* **What was Implemented**:
  * `SortExecutor`: ORDER BY ASC/DESC stable multi-expression sorting.
  * `LimitExecutor`: LIMIT clause tuple capping.
  * `AggregateExecutor`: COUNT(*), COUNT(col), SUM, AVG, MIN, MAX, with optional GROUP BY grouping hash-map.
  * `ExecutionEngine` query pipeline integration: `SeqScan -> [Aggregate] -> [Sort] -> [Limit] -> [Projection]`.
* **Build Command**: `cmake --build build --config Debug`
* **Test Command**: `.\build\bin\emberdb_tests.exe`
* **Test Results**: 23 test cases, 1272 assertions passed, 0 failures.

### Phase 6 — JOINs (Nested Loop Join, INNER / LEFT)
* **Status**: **COMPLETE**
* **Completion Gate**: **PASSED**
  * All Phase 6 gate scenarios verified:
    - Matching rows (INNER JOIN)
    - No matching rows (false predicate)
    - Multiple matches per outer row
    - LEFT JOIN with unmatched rows generating NULLs
    - JOIN + WHERE filter
    - JOIN + projection of qualified column names
    - JOIN + ORDER BY
    - JOIN + LIMIT
    - Multi-table 3-way join composition
  * All 24 test cases (1405 assertions) passed with zero errors.
* **What was Implemented**:
  * `FilterExecutor`: General-purpose Volcano predicate evaluation filter.
  * `NestedLoopJoinExecutor`: Inner & Left outer joins with Volcano iterator semantics and proper inner loop rewind.
  * Schema & Expression resolution: Suffix and prefix resolution in `Schema::GetColIdx` with ambiguity detection and support for `table.column` qualified references.
  * `ExecutionEngine` join pipeline: Automatic schema synthesis, multi-join chaining, and filter execution.
* **Build Command**: `cmake --build build --config Debug`
* **Test Command**: `.\build\bin\emberdb_tests.exe`
* **Test Results**: 24 test cases, 1405 assertions passed, 0 failures.

### Phase 7 — Buffer Pool (LRU, Pin/Unpin, Dirty Tracking)
* **Status**: **COMPLETE**
* **Completion Gate**: **PASSED**
  * All Phase 7 gate scenarios verified:
    - LRU eviction policy with pinned/unpinned frame management.
    - Buffer pool page allocation, fetching, dirty page tracking, and evictions.
    - Buffer pool smaller than total database pages: 150 records across 6 pages accessed through a tiny 3-frame buffer pool without corruption.
    - Persistence across restart with 100% bit-exact tuple verification.
  * All 27 test cases (2045 assertions) passed with zero errors.
* **What was Implemented**:
  * `Replacer` & `LRUReplacer`: Thread-safe LRU eviction queue tracking unpinned frame IDs.
  * `BufferPoolManager`: Manages in-memory `Page` frames, hash table page directory, dirty tracking, eviction, and disk I/O coordination.
  * `TableHeap` & `Catalog` integration: Unified storage layer where relation data and catalog metadata route through the buffer pool.
  * Automatic flush coordination: Flush hooks in `DiskManager` ensure dirty pages are flushed prior to closing.
* **Build Command**: `cmake --build build --config Debug`
* **Test Command**: `.\build\bin\emberdb_tests.exe`
* **Test Results**: 27 test cases, 2045 assertions passed, 0 failures.

### Phase 8 — B+ Tree Index (Search, Insert, Split, Range Scan)
* **Status**: **COMPLETE**
* **Completion Gate**: **PASSED**
  * All Phase 8 gate scenarios verified:
    - 1 insertion: single root leaf node, exact key lookup, range scan, absent key verification.
    - 100 insertions: leaf splitting, internal node creation, exact point lookups for all 100 keys, range scans across multiple leaves.
    - 1,000 insertions: multiple leaf splits, multiple internal splits, root splitting (tree height > 2), exact lookups for all 1,000 keys, full ascending scan verification.
    - Node splitting: leaf splits with bidirectional sibling link maintenance, internal node splitting with middle-key push-up.
    - Root creation and root splitting: verified dynamic root page ID tracking.
    - Range scans: bounded ranges, open-ended scans, and cross-leaf boundary traversals.
    - `CREATE INDEX` integration: `CREATE INDEX idx_name ON table(col);` via SQL, populating index with existing table tuples and maintaining index on subsequent inserts.
    - Persistence across restart: full restart persistence verified bit-for-bit across process close and reopen with zero data loss.
  * All 32 test cases (12,724 assertions) passed with zero errors.
* **What was Implemented**:
  * `IndexKey`: Fixed 128-byte trivially copyable POD structure for universal scalar keys (INTEGER, BIGINT, DOUBLE, BOOLEAN, VARCHAR up to 120 bytes) without dynamic heap pointers.
  * `BPlusTreePage`: 32-byte base header layout on 4096-byte fixed page frames.
  * `BPlusTreeLeafPage`: Leaf node layout storing sorted `(IndexKey, RID)` entries with bidirectional sibling pointers (`next_page_id`, `prev_page_id`).
  * `BPlusTreeInternalPage`: Internal node layout storing child page IDs and routing keys.
  * `BPlusTreeIndex`: Thread-safe B+ Tree engine orchestrating root creation, leaf navigation, node splitting, tree height growth, point lookups, and range scans through `BufferPoolManager`.
  * `IndexInfo` & `Catalog` integration: Schema catalog stores index metadata on page 0 alongside table schemas, with automatic root page ID tracking and reload on startup.
  * `ExecutionEngine` integration: Completed `ExecuteCreateIndex` and wired index synchronization into `ExecuteInsert`.
* **Build Command**: `cmake --build build --config Debug`
* **Test Command**: `ctest --test-dir build --output-on-failure` & `.\build\bin\emberdb_tests.exe`
* **Test Results**: 32 test cases, 12,724 assertions passed, 0 failures.

### Phase 9 — Query Planner + Index Scan
* **Status**: **COMPLETE**
* **Completion Gate**: **PASSED**
  * All Phase 9 gate scenarios verified:
    - Automatic `IndexScan` selection for equality predicate (`WHERE id = 500`) when index exists on `id`.
    - Fallback to `SeqScan` + `Filter` before index creation or for non-indexed columns (`WHERE name = 'User500'`).
    - Range index scan selection (`WHERE id >= 100 AND id <= 120`).
    - IndexScan with residual predicate (`WHERE id = 251 AND age > 20`), where index satisfies `id = 251` and filter operator evaluates `age > 20`.
    - `EXPLAIN` query statement: verified text plan representation exposing physical operators (`IndexScan`, `SeqScan`, `Filter`, `Projection`, etc.).
    - Strict result equivalence: verified that queries executed via `IndexScan` return identical tuples to `SeqScan`.
  * All 33 test cases (14,795 assertions) passed with zero errors.
* **What was Implemented**:
  * `AbstractPlanNode` & physical plan nodes: `SeqScanPlanNode`, `IndexScanPlanNode`, `FilterPlanNode`, `ProjectionPlanNode`, `SortPlanNode`, `LimitPlanNode`, `AggregatePlanNode`, `NestedLoopJoinPlanNode` with tree visualization (`ToString()`).
  * `IndexScanExecutor`: Volcano iterator querying `BPlusTreeIndex` for matching `RID`s and fetching tuples from `TableHeap`.
  * `Planner`: Rule-based query optimizer analyzing AST WHERE expressions, extracting indexable predicates, and selecting between `SeqScanPlanNode` and `IndexScanPlanNode` (point lookup or range scan), attaching residual filters where appropriate.
  * `EXPLAIN` statement support: Tokenizer, AST `ExplainStatement`, parser, and execution returning plan text strings.
  * AST `Clone()` virtual method across all `Expression` variants to safely construct plan trees without mutating parser ASTs.
* **Build Command**: `cmake --build build --config Debug`
* **Test Command**: `ctest --test-dir build --output-on-failure` & `.\build\bin\emberdb_tests.exe`
* **Test Results**: 33 test cases, 14,795 assertions passed, 0 failures.

### Phase 10 — Transactions
* **Status**: **COMPLETE**
* **Completion Gate**: **PASSED**
  * All Phase 10 gate scenarios verified:
    - Commit test: multiple DML statements wrapped in `BEGIN ... COMMIT` persisted to disk and verified surviving process close and restart.
    - Rollback test on INSERT: `BEGIN; INSERT INTO users VALUES (10, 'Test'); ROLLBACK;` confirmed 0 rows returned on subsequent lookup.
    - Rollback test on UPDATE: `BEGIN; UPDATE users SET age = 99 WHERE id = 1; ROLLBACK;` reverted in-place to previous tuple values.
    - Rollback test on DELETE: `BEGIN; DELETE FROM users WHERE id = 2; ROLLBACK;` revived deleted row with exact original values.
    - Multiple mixed statements in transaction: INSERT, UPDATE, and DELETE executed and rolled back cleanly in reverse topological order.
    - Transaction state validation: strict verification of state transitions (`ACTIVE` -> `COMMITTED`, `ACTIVE` -> `ABORTED`), rejection of nested `BEGIN`, rejection of `COMMIT`/`ROLLBACK` without active transaction.
    - Index synchronization on transaction rollback: B+ tree indexes automatically remove newly inserted keys on rollback and restore previous keys on rollback of UPDATE and DELETE.
  * All 35 test cases (14,883 assertions) passed with zero errors.
* **What was Implemented**:
  * `Transaction`: encapsulates `txn_id_t`, `TransactionState` (`ACTIVE`, `COMMITTED`, `ABORTED`), and table write undo logs (`TableWriteRecord`).
  * `TransactionManager`: thread-safe coordinator allocating monotonic transaction IDs and executing ACID `Commit()` and `Abort()` operations.
  * Rollback undo engine: reverses table mutations (tombstoning inserts, rolling back updates with before-images, and resurrecting tombstoned slots in `SlottedPage` via `RollbackDelete`).
  * B+ Tree Index rollback synchronization: added `Remove()` to `BPlusTreeLeafPage` and `BPlusTreeIndex` to keep secondary indexes consistent across rollbacks.
  * SQL parser & lexer integration: added `KEYWORD_TRANSACTION` and parsed `BEGIN [TRANSACTION];`, `COMMIT [TRANSACTION];`, and `ROLLBACK [TRANSACTION];`.
  * `ExecutionEngine` transaction lifecycle: tracks active transaction, logs DML writes, and provides auto-flush buffer pool durability upon `COMMIT`.
* **Build Command**: `cmake --build build --config Debug`
* **Test Command**: `ctest --test-dir build --output-on-failure` & `.\build\bin\emberdb_tests.exe`
* **Test Results**: 35 test cases, 14,883 assertions passed, 0 failures.

### Phase 11 — Concurrency
* **Status**: **COMPLETE**
* **Completion Gate**: **PASSED**
  * All Phase 11 gate scenarios verified:
    - Concurrent buffer pool operations: 8 worker threads concurrently fetching, modifying, unpinning, and evicting pages under small buffer pool constraints with zero corruption or data races.
    - Concurrent table insertions & scans: 4 threads concurrently inserting 200 records into `TableHeap` with full tuple integrity.
    - Concurrent catalog reader-writer synchronization: multiple concurrent reader threads inspecting schemas while writer threads create tables and indexes without races or deadlocks.
    - Concurrent B+ tree index mutations: 4 threads concurrently inserting distinct keys into the same `BPlusTreeIndex` with real-time point-lookup verification and complete scan reachability.
    - Concurrent transactions: multiple committing and aborting transaction worker threads running simultaneously, strictly isolating committed rows and ensuring zero leak of rolled-back rows.
  * All 40 test cases (16,971 assertions) passed with zero errors.
* **What was Implemented**:
  * `Page` reader-writer latching: added `std::shared_mutex rwlock_` with `WLatch()`, `WUnlatch()`, `RLatch()`, and `RUnlatch()` methods.
  * `Catalog` reader-writer synchronization: integrated `mutable std::shared_mutex catalog_latch_`, guarding reader operations with `std::shared_lock` and writer operations with `std::unique_lock`, with reentrancy avoidance via `PersistCatalogUnlocked()`.
  * `TableHeap` synchronization: added `mutable std::mutex latch_` protecting record insertions, point-lookups, updates, deletions, and slotted page allocations.
  * `BPlusTreeIndex` locking: locked `mutex_` across all public API methods (`GetValue`, `GetOneValue`, `Insert`, `Remove`, `ScanRange`, `ScanAll`).
  * Concurrency test suite: implemented comprehensive multithreaded test cases in `tests/concurrency/concurrency_test.cpp`.
* **Build Command**: `cmake --build build --config Debug`
* **Test Command**: `ctest --test-dir build --output-on-failure` & `.\build\bin\emberdb_tests.exe`
* **Test Results**: 40 test cases, 16,971 assertions passed, 0 failures.

### Phase 12 — Write-Ahead Logging (WAL)
* **Status**: **COMPLETE**
* **Completion Gate**: **PASSED**
  * All Phase 12 gate scenarios verified:
    - `LogRecord` binary serialization & deserialization across all record variants (`BEGIN`, `COMMIT`, `ABORT`, `INSERT`, `UPDATE`, `DELETE`) with exact payload reconstruction.
    - Monotonically increasing LSN assignment and tracking across appends.
    - WAL buffer management with on-demand and size-triggered flushing to disk.
    - LSN persistence across restart: reopening an existing WAL continues monotonically from the highest recorded LSN.
    - WAL rule invariant ("log first, data page later") strictly enforced: `BufferPoolManager` flushes WAL records up to the dirty page LSN before writing the database page to disk.
    - `ExecutionEngine` and `TransactionManager` transaction integration: `BEGIN`, mutations, and `COMMIT` are logged into the WAL with backward `prev_lsn` chaining and forced WAL flush on commit.
  * All 45 test cases (17,057 assertions) passed with zero errors.
* **What was Implemented**:
  - `LogRecord`: Binary-serializable struct with 32-byte header, storing transaction ID, LSN, previous LSN, record type, target table, RID, and before/after images.
  - `LogManager`: Thread-safe WAL subsystem managing a 64KB append buffer, assigning LSNs, flushing records to an append-only `.wal` file, and scanning records on startup.
  - `BufferPoolManager` WAL protocol enforcement: wired `FlushLogBufferUpTo(page->GetLSN())` prior to any dirty page write to disk.
  - `TransactionManager` & `ExecutionEngine` WAL integration: automatic logging of `BEGIN`, `COMMIT`, `ABORT`, `INSERT`, `UPDATE`, and `DELETE` records with page LSN stamping.
  - Comprehensive WAL test suite in `tests/recovery/wal_test.cpp`.
* **Build Command**: `cmake --build build --config Debug`
* **Test Command**: `ctest --test-dir build --output-on-failure` & `.\build\bin\emberdb_tests.exe`
* **Test Results**: 45 test cases, 17,057 assertions passed, 0 failures.

### Phase 13 — Crash Recovery
* **Status**: **COMPLETE**
* **Completion Gate**: **PASSED**
  * All Phase 13 gate scenarios verified:
    - Clean shutdown detection: clean sessions write `CHECKPOINT_END` and flush pages; subsequent startup confirms `NeedsRecovery() == false`.
    - Unclean shutdown detection: active uncommitted transactions without clean checkpoint trigger `NeedsRecovery() == true`.
    - Redo pass (repeating history): committed transactions that had not yet reached disk at the crash point are fully replayed into table heaps and secondary B+ tree indexes.
    - Undo pass (rolling back losers): uncommitted/active transactions present at crash time are completely rolled back in reverse LSN order (insert deletion, before-image update restoration, and tombstone rollback deletion), restoring original table and index states.
    - End-to-end continuous operation after recovery: immediate support for new queries (`INSERT`, `SELECT`, `UPDATE`) on top of recovered relations, followed by clean shutdown and bit-exact restart verification.
  * All 49 test cases (17,144 assertions) passed with zero errors.
* **What was Implemented**:
  - `RecoveryManager`: Implements three-pass ARIES-style crash recovery (`AnalysisPass`, `RedoPass`, `UndoPass`) and `RecordCleanShutdown()`.
  - `SlottedPage::RedoInsert`: Low-level slotted page slot expansion and tuple replay hook for bit-exact redo operations.
  - `EmberDBInstance`: Unified top-level database engine class integrating `DiskManager`, `BufferPoolManager`, `Catalog`, `LogManager`, `RecoveryManager`, and `ExecutionEngine` with automated startup recovery and clean shutdown checkpoints.
  - Comprehensive crash recovery test suite in `tests/recovery/recovery_test.cpp`.
* **Build Command**: `cmake --build build --config Debug`
* **Test Command**: `ctest --test-dir build --output-on-failure` & `.\build\bin\emberdb_tests.exe`
* **Test Results**: 49 test cases, 17,144 assertions passed, 0 failures.

### Phase 14 — CLI Polish
* **Status**: **COMPLETE**
* **Completion Gate**: **PASSED**
  * All Phase 14 gate scenarios verified:
    - Interactive shell with multi-line query editing and continuation prompt (`   ...> `).
    - Single-line SQL comment filtering (`-- ...`).
    - Full suite of meta-commands:
      * `.help`: command descriptions and usage.
      * `.tables`: catalog table listing.
      * `.schema [table]`: clean column name and type alignment matching AGENTS.md format.
      * `.indexes [table]`: secondary index introspection on tables.
      * `.stats`: engine statistics (table counts, buffer pool frames, WAL LSN tracking).
      * `.history`: in-session query history.
      * `.version`: engine version.
      * `.exit`, `.quit`: graceful shutdown with clean checkpoint.
    - One-shot headless execution via `-c "<sql>"` / `--command "<sql>"` verified end-to-end for automated workflows.
    - Clean lifecycle orchestration backed by `EmberDBInstance`.
  * All 49 test cases (17,144 assertions) passed with zero errors.
* **What was Implemented**:
  - Interactive REPL in `src/main.cpp` connected directly to `EmberDBInstance`.
  - Schema, index, statistics, and history display routines with formatted output.
  - Multi-line buffer accumulator and command-line argument parser.
* **Build Command**: `cmake --build build --config Debug`
* **Test Command**: `ctest --test-dir build --output-on-failure` & `.\build\bin\emberdb_tests.exe`
* **Test Results**: 49 test cases, 17,144 assertions passed, 0 failures.

### Phase 15 — HTTP API
* **Status**: **COMPLETE**
* **Completion Gate**: **PASSED**
  * All Phase 15 gate scenarios verified:
    - Native cross-platform C++ HTTP server (`HttpServer`) using Winsock2 on Windows and POSIX sockets on Linux.
    - Endpoints verified with live TCP socket communication and HTTP request/response parsing:
      * `GET /api/health` returns JSON health status, version, and engine name (`{"status":"ok","version":"0.1.0","engine":"EmberDB"}`).
      * `GET /api/tables` returns JSON array of catalog tables.
      * `GET /api/schema/:table` returns detailed column metadata (names and types) or 404 if table does not exist.
      * `POST /api/query` parses JSON payload `{"sql":"..."}`, executes statement through `EmberDBInstance::ExecuteQuery`, and returns execution time, affected row counts, column names, and row values. Returns formatted 400 Bad Request JSON on SQL parsing or execution errors.
      * `OPTIONS /api/*` responds with CORS headers (`Access-Control-Allow-Origin: *`, allowed methods and headers) to allow web browser clients to connect without cross-origin blocking.
    - Result equivalence: Queries executed through the HTTP API return identical data to queries executed via CLI or internal C++ API.
    - Standalone server binary `emberdb_server` created for hosting the database engine over HTTP.
  * All 51 test cases (17,182 assertions) passed with zero errors.
* **What was Implemented**:
  - `HttpServer`: Lightweight native non-blocking/timed HTTP server (`src/include/emberdb/server/http_server.h`, `src/server/http_server.cpp`) with route dispatch, HTTP header parsing, and JSON response generation.
  - Integration with `EmberDBInstance`: Centralized query execution, catalog schema lookup, and JSON serialization of `Tuple` and `Value` results.
  - Server executable in `src/server/server_main.cpp` supporting configurable host and port CLI flags (`--port <port>`, `--host <host>`, `--data-dir <dir>`).
  - Unit and integration tests in `tests/server/api_test.cpp`.
* **Build Command**: `cmake --build build --config Debug`
* **Test Command**: `ctest --test-dir build --output-on-failure` & `.\build\bin\emberdb_tests.exe`
* **Test Results**: 51 test cases, 17,182 assertions passed, 0 failures.

### Phase 16 — React Web Interface
* **Status**: **COMPLETE**
* **Completion Gate**: **PASSED**
  * All Phase 16 gate scenarios verified:
    - Built responsive, clean React + Vite SQL console application in `web/` directory.
    - Verified complete end-to-end execution of all required SQL statement types over HTTP API into the database engine:
      * `CREATE TABLE`: Created `users` and `orders` tables.
      * `INSERT`: Populated records across multiple tables.
      * `SELECT`: Filtered and projected records with `WHERE` and `ORDER BY`.
      * `UPDATE`: Modified rows and validated updated values.
      * `DELETE`: Removed records and validated empty sets.
      * `JOIN`: Inner join between `users` and `orders` on foreign key predicate.
      * `GROUP BY`: Grouped aggregations with `COUNT(*)`.
    - Catalog inspection: Interactive sidebar listing tables (`GET /api/tables`) and column schema details (`GET /api/schema/:table`).
    - Execution metrics: Displays execution time (ms), rows returned, and rows affected.
    - Error display: Clear error banner showing database-level parser and runtime errors.
    - Query history: Preserves executed statements in `localStorage` with re-run on click.
    - Static UI hosting: `emberdb_server` serves the built React web application directly from `web/dist` on `GET /` and `/assets/*`.
    - Production build: `npm run build` cleanly compiled Vite bundle with zero errors or warnings.
  * All 52 test cases (17,208 assertions) passed with zero errors.
* **What was Implemented**:
  - React 18 + Vite frontend in `web/` (`package.json`, `vite.config.js`, `index.html`, `src/App.jsx`, `src/index.css`).
  - Static asset serving within `HttpServer` (`src/server/http_server.cpp`) to serve the built Web UI directly from `web/dist`.
  - Phase 16 completion gate test suite in `tests/server/api_test.cpp`.
* **Build Command**: `cd web ; npm run build` & `cmake --build build --config Debug`
* **Test Results**: 52 test cases, 17,208 assertions passed, 0 failures.

### Phase 17 — Integration + Final Hardening
* **Status**: **COMPLETE**
* **Completion Gate**: **PASSED**
  * All Phase 17 gate scenarios verified:
    - End-to-end multi-table relation tests with `departments` and `employees`:
      * Secondary B+ tree indexes created and queried via index point-lookups.
      * Multi-table `INNER JOIN` and `LEFT JOIN` queries combining predicates, projections, and `ORDER BY`.
      * Aggregations with `GROUP BY` (`COUNT(*)`, etc.).
      * Full database persistence verified across clean process shutdown and reopening.
    - Crash recovery and ACID invariance under mixed workloads:
      * Tested committed transactions + uncommitted active loser transactions surviving sudden unclean termination.
      * RecoveryManager automatically performed ARIES-style analysis, redid committed changes, and undid uncommitted loser modifications (reversing inserts, restoring update before-images, and reviving deleted rows).
      * Continued operations immediately resumed post-recovery without data corruption.
    - High-concurrency multithreaded stress testing:
      * 6 concurrent worker threads performing hundreds of simultaneous inserts and index point queries across shared buffer pool, catalog, table heap, and B+ tree indexes.
      * Verified total tuple counts and zero data races, corruptions, or deadlocks.
    - Multi-statement headless CLI execution:
      * Enhanced CLI `-c` flag to parse and execute semicolon-delimited SQL batches non-interactively with tabular output.
  * All 55 test cases (17,296 assertions) passed with zero errors.
* **What was Implemented**:
  - `tests/integration/system_integration_test.cpp`: System integration test suite covering multi-relation joins, indexing, restart persistence, crash recovery, and multithreaded stress testing.
  - `EmberDBInstance::SimulateCrash()`: Method enabling controlled abrupt termination without writing clean shutdown markers to test ARIES recovery.
  - Multi-statement batch execution in `src/main.cpp` for non-interactive scripting workflows.
* **Build Command**: `cmake --build build --config Debug`
* **Test Command**: `ctest --test-dir build --output-on-failure` & `.\build\bin\emberdb_tests.exe`
* **Test Results**: 55 test cases, 17,296 assertions passed, 0 failures.

### Phase 18 — Documentation + Interview Readiness
* **Status**: **COMPLETE**
* **Completion Gate**: **PASSED**
  * All Phase 18 gate deliverables completed and verified:
    - `README.md`: Polished and structured with system architecture diagrams, quickstart instructions for CLI, HTTP server, and React console, and a comprehensive 10–15 minute "How EmberDB Works" technical interview guide.
    - `docs/architecture.md`: Complete three-tier system architecture specification covering Interface, Query Engine, and Storage Engine.
    - `docs/storage-format.md`: Low-level byte layouts documenting slotted pages, 24-byte headers, slot directory mechanics, tuple binary format, B+ Tree node layout, and WAL record binary structure.
    - `docs/sql.md`: Complete SQL specification covering supported data types, DDL, DML, Joins, Aggregations, GROUP BY, Transactions, and query inspection via `EXPLAIN`.
    - `docs/transactions.md`: Comprehensive ACID specification detailing the transaction state machine, in-memory undo logging (`TableWriteRecord`), reverse-topological rollback mechanics, and multi-level latching.
    - `docs/recovery.md`: Deep dive into Write-Ahead Logging invariants, 32-byte WAL record header format, and three-pass ARIES crash recovery (Analysis, Redo, Undo).
    - `docs/design-decisions.md`: Complete set of 17 Architecture Decision Records (ADRs 001–017) capturing context, choices, alternatives, and tradeoffs.
  * All 55 test cases (17,296 assertions) passed with zero errors.
* **What was Implemented**:
  - Full subsystem documentation across `README.md` and all files in `docs/`.
  - Step-by-step interview walkthrough explaining query flow from character tokens to disk blocks and back.
* **Build Command**: `cmake --build build --config Debug`
* **Test Command**: `ctest --test-dir build --output-on-failure` & `.\build\bin\emberdb_tests.exe`
* **Test Results**: 55 test cases, 17,296 assertions passed, 0 failures.
