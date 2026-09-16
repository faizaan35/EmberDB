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
| **Phase 13** | Crash Recovery (Redo, Undo, Checkpointing) | PENDING | NOT STARTED |
| **Phase 14** | CLI Polish (Interactive REPL, Meta Commands) | PENDING | NOT STARTED |
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





