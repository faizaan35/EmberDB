# EmberDB Architectural Decision Records (ADRs)

## ADR-001: Selection of C++17 Standard

* **Context**: Modern database engines require low-level memory control, predictable performance, and strong type safety.
* **Decision**: Adopt ISO C++17 as the core language standard for EmberDB.
* **Why**: C++17 provides essential vocabulary types (`std::variant`, `std::optional`, `std::string_view`, structured bindings, `std::shared_mutex`) while enjoying universal compiler support across Windows (MSVC/MinGW GCC) and Linux (GCC/Clang).
* **Alternatives Considered**:
  * C++14: Lacks `std::variant` and `std::optional`, necessitating external dependencies.
  * C++20: Module and coroutine support varies across compilers and toolchains.
* **Tradeoffs**: Requires modern compiler toolchains; manual memory ownership requires rigorous RAII hygiene.

---

## ADR-002: Fixed 4096-Byte Slotted Page Format

* **Context**: Database disk persistence requires a structured layout to store tuples of varying lengths while maintaining stable row identifiers.
* **Decision**: Standardize on a fixed page size of 4096 bytes (4 KB) with a bidirectional slotted-page layout.
* **Why**: 4 KB matches the standard virtual memory page size of modern operating systems and hardware SSD block sectors, minimizing read/write amplification. Slotted pages allow records to move during fragmentation cleanup without altering external Record Identifiers (RIDs).
* **Alternatives Considered**:
  * Fixed-length records per page: Wasteful for variable-length strings like `VARCHAR`.
  * Append-only log-structured pages: Complex compaction and slower point-lookup reads.
* **Tradeoffs**: Records larger than ~4000 bytes require overflow/spill pages.

---

## ADR-003: Catch2 Testing Framework

* **Context**: Subsystem verification must be automated, rigorous, and capable of unit and integration test suites without external network dependencies during compilation.
* **Decision**: Adopt Catch2 (single-header distribution) as the primary test framework.
* **Why**: Catch2 enables natural BDD-style assertions (`REQUIRE`, `CHECK`), structured test sections (`SECTION`), clear failure diffs, zero runtime dependencies, and seamless CTest integration. The single-header distribution ensures full offline reproducibility.
* **Alternatives Considered**:
  * GoogleTest: Requires additional CMake target compilation and external download.
  * Custom assert macros: Lacks structured failure reporting, test filtering, and fixture management.
* **Tradeoffs**: Longer single-file compilation time for the translation unit that defines `CATCH_CONFIG_MAIN`.

---

## ADR-004: Explicit Status and Result<T> Error Handling

* **Context**: Database internals experience expected errors (e.g. disk full, table not found, syntax error) that should be handled gracefully without silent corruption or unhandled crashes.
* **Decision**: Implement an explicit `Status` enum/class and a monad-like `Result<T>` template for non-exceptional error propagation, reserving C++ exceptions for unexpected invariant violations.
* **Why**: Aligns with modern systems programming (Google Abseil `absl::Status`, Rust `Result<T, E>`). Forces callers to explicitly check outcomes, avoiding hidden control flow paths and overhead from exception unwinding in performance-critical execution loops.
* **Alternatives Considered**:
  * Pure C++ exception hierarchy (`std::runtime_error`): Exception unwinding can obscure error origins and incurs performance penalties.
  * C-style error codes: Vulnerable to ignored return values and lack context strings.
* **Tradeoffs**: Slightly more verbose return-value checking at function call sites.

---

## ADR-008: LRU-Managed Buffer Pool Architecture

* **Context**: Disk I/O is the primary performance bottleneck in relational database engines. Database systems must cache pages in memory, arbitrate frame eviction, and maintain dirty state without tying memory management to SQL execution logic.
* **Decision**: Implement a dedicated `BufferPoolManager` with an `LRUReplacer` managing in-memory 4096-byte `Page` frames.
* **Why**: Explicit pin/unpin semantics ensure that pages actively referenced by executors or B+ Tree traversals cannot be evicted mid-operation. The buffer pool enforces strict isolation between storage algorithms and physical disk I/O.
* **Alternatives Considered**:
  * OS `mmap`: Unpredictable page writeback timing, lack of explicit pin/unpin guarantees, vulnerability to SIGBUS on file truncation.
  * Clock (Second-Chance) replacement: Simple, but LRU provides well-understood deterministic behavior for educational inspection and verification.
* **Tradeoffs**: In-memory hash table lookups and frame locking introduce synchronization overhead.

---

## ADR-009: Page-Oriented B+ Tree Index with Fixed 128-Byte Universal Keys

* **Context**: Efficient relational query execution requires ordered logarithmic indexes (`O(log N)`) for equality lookups and range scans. The index must persist to disk blocks via the buffer pool without storing raw heap pointers.
* **Decision**: Implement a native B+ Tree whose internal and leaf nodes map directly to 4096-byte `Page` buffers, utilizing a self-contained 128-byte `IndexKey` union for universal scalar key storage.
* **Why**:
  1. All EmberDB scalar types (`INTEGER`, `BIGINT`, `DOUBLE`, `BOOLEAN`, and `VARCHAR` up to 120 bytes) can be stored inline inside page frames with 8-byte alignment, avoiding pointer swizzling and heap fragmentation on disk.
  2. With a node capacity of 29 entries, small test datasets (100 and 1,000 insertions) realistically trigger leaf splits, internal splits, and multi-level root splits.
  3. Bidirectional sibling pointers (`next_page_id`, `prev_page_id`) enable linear `O(K)` range scan traversals without parent backtracking.
  4. Integration with `Catalog` page 0 provides durable persistence and transparent index recovery across process restarts.
* **Alternatives Considered**:
  * In-memory `std::map` or AVL tree: Violates the core database engine rule requiring page-based on-disk persistence.
  * Slotted page layout for index nodes: Excessive complexity for sorted binary search arrays; fixed slot entry sizes allow direct $O(\log N)$ binary search without offset indirection.
* **Tradeoffs**: Key size is capped at 120 bytes for `VARCHAR` columns; fixed 128-byte slot size trades higher fanout for integer keys in exchange for universal type support and simplicity.

---

## ADR-010: In-Memory Undo-Log Transaction Subsystem with Storage Rollback Hooks

* **Context**: A relational database must provide Atomicity and Durability for multi-statement operations (`BEGIN`, `COMMIT`, `ROLLBACK`). If a transaction aborts or errors out, all intermediate mutations across tables and indexes must be rolled back.
* **Decision**: Implement a `Transaction` class tracking transaction state (`ACTIVE`, `COMMITTED`, `ABORTED`) and a reverse undo log (`TableWriteRecord`) recording before- and after-images of INSERT, UPDATE, and DELETE operations. Pair this with dedicated storage engine resurrection hooks (`SlottedPage::RollbackDelete`) and index removal hooks (`BPlusTreeIndex::Remove`).
* **Why**:
  1. Operating in reverse topological order during rollback ensures that cascaded operations (e.g. INSERT followed by UPDATE or DELETE) cleanly restore previous state.
  2. `SlottedPage::RollbackDelete` allows tombstoned slots (`s.size == 0`) to be revived with their original before-image at the exact same `RID` without changing tuple identifiers.
  3. `BPlusTreeIndex::Remove` synchronizes secondary indexes during rollbacks, ensuring subsequent point lookups and range scans remain bit-exact.
  4. Explicit `COMMIT` invokes `BufferPoolManager::FlushAllPages()`, guaranteeing disk persistence across process restarts.
* **Alternatives Considered**:
  * Shadow paging: Copying entire pages on write produces severe disk fragmentation and complicates index maintenance.
  * Direct WAL rollback without undo log: Requires WAL replay subsystem (Phase 12/13) before transactions can even function. The in-memory undo log provides immediate transaction atomicity while paving the way for WAL integration.
* **Tradeoffs**: Active transaction undo logs reside in memory during transaction execution; very large uncommitted transactions require memory proportional to the number of modified records.

---

## ADR-011: Hierarchical Concurrency Control and Subsystem Synchronization

* **Context**: Multiple worker threads, client sessions, and background flushing tasks access shared database subsystems simultaneously. Without explicit synchronization, data races, corrupted page directory tables, lost updates, and deadlocks occur.
* **Decision**: Adopt a clear hierarchical synchronization strategy using ISO C++17 primitives:
  1. `Page`: per-page reader-writer latch (`std::shared_mutex rwlock_`) providing `WLatch`/`RLatch` semantics for memory frame read/write isolation.
  2. `BufferPoolManager`: dedicated frame allocation mutex (`std::mutex mutex_`) protecting the frame lookup hash table, free list, pin counts, and LRU replacer victim selection.
  3. `Catalog`: shared mutex (`mutable std::shared_mutex catalog_latch_`) allowing multiple concurrent schema readers (`std::shared_lock`) while serializing catalog modifications (`std::unique_lock`). Reentrant deadlock during page-0 persistence is prevented via `PersistCatalogUnlocked()`.
  4. `TableHeap`: table-level mutex (`mutable std::mutex latch_`) synchronizing page chaining, record insertion, and slotted page space allocation.
  5. `BPlusTreeIndex`: index-level mutex (`mutable std::mutex mutex_`) synchronizing tree traversals, node splitting, key lookups, and range scans.
  6. `TransactionManager`: internal mutex (`std::mutex latch_`) synchronizing transaction ID allocation and active transaction map state.
* **Why**:
  - Distributing synchronization across functional subsystems prevents a single global database lock bottleneck.
  - Reader-writer locks on `Catalog` and `Page` maximize concurrency for read-heavy workloads (concurrent queries).
* **Alternatives Considered**:
  - Coarse-grained single global database lock: Trivial to implement, but serializes all operations and causes severe contention under concurrent workloads.
  - Lock-free data structures: High complexity, difficult to maintain, and prone to subtle ABA and memory-ordering bugs.
* **Tradeoffs**: Mutex acquisition overhead on critical paths; strict lock acquisition order must be maintained to avoid deadlocks.

---

## ADR-012: Write-Ahead Logging (WAL) Architecture and LSN Invariant Enforcement

* **Context**: Relational databases must uphold durability and prepare for crash recovery. The core WAL protocol dictates that log records describing data mutations must reach persistent storage *before* the corresponding modified database page is written to disk.
* **Decision**: Implement `LogRecord`, `LogManager`, and explicit integration with `BufferPoolManager`:
  1. `LogRecord`: Binary-serializable structure with a 32-byte header (`size`, `lsn`, `prev_lsn`, `txn_id`, `type`) and payload supporting `BEGIN`, `COMMIT`, `ABORT`, `INSERT`, `UPDATE`, and `DELETE`. Records capture target table names, RIDs, and bit-exact before/after tuple images.
  2. `LogManager`: Coordinates a 64KB append buffer, assigns monotonically increasing `lsn_t` values, and flushes log records to an append-only WAL file (`.wal`). Reopening an existing WAL scans and restores `next_lsn` to continue without sequence reset.
  3. WAL Invariant Enforcement: `BufferPoolManager` holds a pointer to `LogManager`. Before any dirty page is written to disk via `disk_mgr_->WritePage()`, BPM invokes `log_mgr_->FlushLogBufferUpTo(page.GetLSN())`, guaranteeing that WAL disk writes strictly precede database page disk writes.
  4. Transaction durability: `TransactionManager::Commit()` appends a `COMMIT` record and forces a synchronous flush up to the commit LSN.
* **Why**:
  - Enforcing the WAL invariant inside `BufferPoolManager` guarantees correctness regardless of whether page eviction occurs due to LRU replacement or explicit `FlushAllPages()`.
  - Transaction undo chains (`prev_lsn`) allow fast backward traversal during aborts or recovery without scanning unrelated records.
* **Alternatives Considered**:
  - Direct synchronous disk write on every log append: Degrades throughput due to excessive small I/O syscalls. An in-memory buffer with forced flush on commit and page flush provides high performance while guaranteeing ACID durability.
* **Tradeoffs**: Memory overhead of 64KB log buffer and minor disk write amplification from WAL records.

---

## ADR-013: Crash Recovery with Unclean Shutdown Detection, Redo, and Undo Passes

* **Context**: After a power outage, process abort, or operating system crash, the persistent database files may be in an inconsistent state: committed transactions might have had dirty pages still in memory (not flushed to disk), and uncommitted "loser" transactions might have written dirty pages to disk prior to the crash.
* **Decision**: Implement `RecoveryManager` executing a three-pass ARIES-style algorithm:
  1. Unclean Shutdown Detection: Clean shutdowns append a `CHECKPOINT_END` record to the WAL and flush all dirty pages. On startup, `RecoveryManager::NeedsRecovery()` scans the WAL; if the last record is not a clean checkpoint or if active transactions exist, recovery is triggered.
  2. Analysis Pass: Scans the log from start to finish to reconstruct transaction states (`active_txns` vs `committed_txns`) and determine the highest LSN in the log.
  3. Redo Pass ("Repeating History"): Iterates forward through all DML log records in ascending LSN order. If a page's on-disk LSN is less than the log record LSN, the mutation (`INSERT`, `UPDATE`, `DELETE`) is re-applied to the `SlottedPage` and secondary indexes, bringing page state exactly to the crash point.
  4. Undo Pass ("Rolling Back Losers"): Iterates backward through log records in reverse LSN order. Any mutations originating from active/uncommitted loser transactions are reversed (`DeleteRecord` for inserts, `UpdateRecord` with `before_image` for updates, `RollbackDelete` for deletes). An `ABORT` record is written for each loser transaction.
  5. Post-Recovery Stabilization: Flushes all recovered pages and records a new `CHECKPOINT_END` marker. The database then immediately accepts normal client traffic.
* **Why**:
  - The repeating-history principle ensures that all intermediate physical page states are accurately rebuilt before rolling back uncommitted changes.
  - Bidirectional index synchronization ensures secondary B+ tree indexes remain 100% consistent with table heaps across crashes.
* **Alternatives Considered**:
  - Shadow-paging recovery: High disk fragmentation and complicated multi-file management.
  - Re-executing SQL text: Non-deterministic and unable to preserve physical record identifiers (RIDs) and secondary index linkages.
* **Tradeoffs**: Recovery time scales with the number of log records since the last checkpoint.

---

## ADR-014: Unified Database Instance and Interactive CLI Shell

* **Context**: The database engine consists of multiple cooperating subsystems (`DiskManager`, `BufferPoolManager`, `Catalog`, `LogManager`, `RecoveryManager`, `ExecutionEngine`). Exposing an accessible CLI shell and programmatic API requires a cohesive facade that manages system lifecycle, crash recovery on startup, and clean checkpoints on shutdown.
* **Decision**: Implement `EmberDBInstance` as the top-level orchestrator and polish the CLI REPL in `src/main.cpp`:
  1. `EmberDBInstance`: Encapsulates database directory paths, instantiates all internal layers, executes `RecoveryManager::Recover()` automatically if an unclean shutdown is detected, and records a clean shutdown checkpoint upon `Close()`.
  2. Interactive CLI REPL: Supports multi-line input (`   ...> ` continuation prompt), SQL comment stripping, formatted ASCII output with execution timings, and shell meta-commands (`.tables`, `.schema [table]`, `.indexes [table]`, `.stats`, `.history`, `.version`, `.help`, `.exit`).
  3. Non-interactive script execution: Supports `-c "<sql>"` / `--command "<sql>"` flag for scripting, automated testing, and CI pipelines without terminal interactivity.
* **Why**:
  - Encapsulating the engine inside `EmberDBInstance` guarantees that the CLI, HTTP API server, and automated integration tests interact with the exact same database lifecycle.
  - One-shot command execution enables headless verification without third-party CLI automation tools.
* **Alternatives Considered**:
  - Independent subsystem instantiation in `main.cpp`: Leaves lifecycle coordination ad-hoc, increasing risk of forgotten buffer flushes or skipped recovery steps.
* **Tradeoffs**: Minor binary size increase for formatting and CLI command dispatching.


---

## ADR-015: Native C++ HTTP REST API Server and JSON Serialization

* **Context**: The database engine needs to expose an HTTP REST interface for remote clients, web user interfaces, and automated integration tooling, without compromising the core rule that all database functionality remains our own implementation.
* **Decision**: Implement a native C++ HTTP server (`HttpServer`) using platform socket APIs (Winsock2 on Windows, POSIX sockets on Linux):
  1. Socket management: `HttpServer` listens on a configurable host and port in a dedicated background accept thread. The listening socket employs a 500ms receive timeout (`SO_RCVTIMEO`) to unblock periodically and inspect the `is_running_` atomic flag, ensuring responsive and graceful shutdown.
  2. Request Dispatch: Supports `GET /api/health`, `GET /api/tables`, `GET /api/schema/:table`, `POST /api/query`, and preflight `OPTIONS` for browser CORS compatibility (`Access-Control-Allow-Origin: *`, `Access-Control-Allow-Methods`, `Access-Control-Allow-Headers`).
  3. JSON Serialization: Implemented lightweight JSON serialization helpers for query parameters, schema descriptors, row tuples, and formatted error messages without bringing in heavyweight external third-party dependencies.
  4. Execution Pipeline: Incoming SQL queries are dispatched directly to the same `EmberDBInstance::ExecuteQuery` used by the CLI and integration tests.
* **Why**:
  - Keeps the network layer clean, lightweight, and completely separate from query parsing, planning, execution, and storage.
  - Native socket implementation avoids adding external web framework dependencies while remaining 100% compliant with HTTP/1.1 REST specifications.
  - Single database instance facade guarantees uniform behavior between terminal CLI and HTTP clients.
* **Alternatives Considered**:
  - Heavy external HTTP libraries (e.g., Boost.Beast, Crow, Drogon): Introduces complex external build dependencies contrary to the educational and clean C++17 focus of the project.
  - Dedicated Python or Node sidecar bridge: Violates the requirement that the database engine itself provides its own HTTP endpoint in C++.
* **Tradeoffs**: HTTP/1.1 keep-alive and chunked transfer are omitted in favor of simple connection-per-request / Content-Length messaging, which is perfectly suited for local database queries.

