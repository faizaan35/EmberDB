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


