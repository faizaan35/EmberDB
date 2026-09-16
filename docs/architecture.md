# EmberDB Architecture Specification

EmberDB is a from-scratch relational database management system designed with clean modular separation across three architectural tiers: the **Interface & Network Tier**, the **Query Compilation & Execution Engine**, and the **Storage & Durability Subsystem**.

---

## High-Level System Architecture

```text
                             Browser (React 18 + Vite)
                                        │
                                        │ HTTP REST (JSON)
                                        ▼
                                ┌───────────────┐
                                │   HttpServer  │ (Winsock2 / POSIX Sockets)
                                └───────┬───────┘
                                        │
                         CLI ───────────┼─────────── Automated Tests
                                        │
                                        ▼
                            ┌───────────────────────┐
                            │   EmberDBInstance     │ (System Facade & Lifecycle)
                            └───────────┬───────────┘
                                        │
              ┌─────────────────────────┼─────────────────────────┐
              ▼                         ▼                         ▼
         SQL Lexer                 Query Planner               Catalog
    (Token Stream)            (IndexScan vs SeqScan)      (Tables, Schemas, Types)
              │                         │                         │
              ▼                         ▼                         │
         SQL Parser              Physical Plan Tree               │
    (Recursive Descent AST)             │                         │
              │                         │                         │
              └─────────────────────────┼─────────────────────────┘
                                        ▼
                                 Query Executor
                          (Volcano Iterator Pipeline)
                                        │
              ┌─────────────────────────┼─────────────────────────┐
              ▼                         ▼                         ▼
          SeqScan                    Joins                    IndexScan
        (TableHeap)             (NestedLoopJoin)             (B+ Tree)
              │                         │                         │
              └─────────────────────────┼─────────────────────────┘
                                        ▼
                                 Storage Engine
                                        │
              ┌─────────────────────────┼─────────────────────────┐
              ▼                         ▼                         ▼
         Slotted Pages             Buffer Pool                 B+ Tree
       (4096B Disk Pages)       (LRU Replacer / Frames)       (Secondary Indexes)
              │                         │                         │
              └─────────────────────────┼─────────────────────────┘
                                        ▼
                                   DiskManager
                           (Block I/O, 4KB Allocation)
                                        │
                                        ▼
                               Write-Ahead Logging
                           (64KB Buffer, LSN Chaining)
                                        │
                                        ▼
                                 Crash Recovery
                           (ARIES Analysis, Redo, Undo)
```

---

## 1. Interface & Network Tier

### 1.1 Command Line Interface (CLI)
* Hand-crafted interactive REPL (`src/main.cpp`) backed directly by `EmberDBInstance`.
* Features multi-line query accumulation (`   ...> ` prompt), SQL comment stripping (`-- ...`), ASCII tabular result formatting with millisecond timings, query history preservation, and administrative meta-commands (`.tables`, `.schema`, `.indexes`, `.stats`, `.history`, `.version`, `.exit`).
* Non-interactive execution flag (`-c "<sql>"`) for shell scripts, continuous integration, and batch processing.

### 1.2 Native C++ HTTP Server (`HttpServer`)
* Cross-platform socket server (`src/server/http_server.cpp`) using Winsock2 on Windows and POSIX sockets on Linux.
* Implements REST endpoints:
  - `GET /api/health`: Health status, version, and engine verification.
  - `GET /api/tables`: JSON array of all persisted catalog tables.
  - `GET /api/schema/:table`: Column names and data types.
  - `POST /api/query`: Receives `{"sql":"..."}`, executes via `EmberDBInstance::ExecuteQuery`, and returns execution timings, column definitions, and row arrays.
  - `OPTIONS /api/*`: CORS preflight headers allowing browser clients to connect without security blocks.
* Static Web UI Hosting: Automatically serves the built React/Vite assets from `web/dist` on `GET /` and `/assets/*`, providing an embedded zero-configuration web console experience.

### 1.3 React Web Console
* Single-page React 18 + Vite application in `web/` with dark-themed dashboard.
* Features SQL editor with `Ctrl+Enter` shortcut, sample query templates, catalog table and schema viewer, result table rendering with NULL handling, error banners, and execution metrics.

---

## 2. Query Compilation & Execution Engine

### 2.1 Lexer & Recursive-Descent Parser
* **Lexer**: Tokenizes raw SQL character streams into strongly-typed `Token` objects (keywords, identifiers, numbers, strings, comparisons, operators).
* **Parser**: Hand-written recursive-descent parser constructing typed Abstract Syntax Tree (AST) nodes without external dependencies (no Flex/Bison).
* Supports DDL (`CREATE TABLE`, `DROP TABLE`, `CREATE INDEX`), DML (`INSERT`, `SELECT`, `UPDATE`, `DELETE`), filtering (`WHERE`), sorting (`ORDER BY ASC/DESC`), pagination (`LIMIT`), joins (`INNER JOIN`, `LEFT JOIN`), aggregations (`COUNT`, `SUM`, `AVG`, `MIN`, `MAX`), groupings (`GROUP BY`), and transactions (`BEGIN`, `COMMIT`, `ROLLBACK`).

### 2.2 System Catalog
* Thread-safe metadata repository tracking all table definitions, column types, sizes, nullability, and secondary B+ tree index bindings.
* Persisted directly to page 0 (`CATALOG_PAGE_ID`) of the database file on change with signature verification (`EMBERDB_CATALOG`).

### 2.3 Rule-Based Query Planner
* Analyzes AST expressions, extracts pushdown predicates, and builds physical execution plan trees:
  - Inspects `WHERE` clauses for equality (`col = val`) or range (`col >= val AND col <= val`) predicates matching existing secondary indexes.
  - Emits `IndexScanPlanNode` when an index matches, attaching any residual predicates to a post-filtering node.
  - Emits `SeqScanPlanNode` with attached `FilterPlanNode` when no matching index exists.
* Supports `EXPLAIN <sql>` statement to inspect the generated physical plan hierarchy.

### 2.4 Volcano Iterator Query Execution
* Standard iterator pattern where each executor implements:
  - `Init()`: Initializes children and allocates state.
  - `Next(Tuple* tuple, RID* rid)`: Returns true and yields the next matching tuple, or false when exhausted.
  - `Close()`: Cleans up transient state.
* **Operators**:
  - `SeqScanExecutor`: Scans `TableHeap` sequentially across slotted pages.
  - `IndexScanExecutor`: Uses B+ Tree index to retrieve candidate `RID`s and fetches tuples.
  - `FilterExecutor`: Evaluates AST expression trees against incoming tuples.
  - `ProjectionExecutor`: Projects and transforms output columns.
  - `NestedLoopJoinExecutor`: Joins outer and inner tuple streams with support for both `INNER` and `LEFT` join semantics.
  - `SortExecutor`: Performs in-memory order-by sorting based on specified columns and direction (`ASC`/`DESC`).
  - `LimitExecutor`: Halts child iteration once the row limit is reached.
  - `AggregateExecutor` & `GroupBy`: Aggregates values into hash tables for `GROUP BY` buckets or scalar reductions.

---

## 3. Storage & Durability Subsystem

### 3.1 Slotted Page Architecture (4096 bytes)
* Fixed-size disk pages matching operating system clusters.
* 24-byte page header tracking `page_id`, `lsn`, `prev_page_id`, `next_page_id`, `slot_count`, and `free_space_pointer`.
* Slot directory grows downwards from the header; variable-length serialized records grow upwards from the bottom of the page.
* Records are addressed by `RID` (`page_id`, `slot_id`). Updates and deletions tombstone or compact slots without altering external `RID` references.

### 3.2 Buffer Pool Manager
* In-memory cache frame array with a Least Recently Used (LRU) replacer.
* Coordinates page pinning (`pin_count`) and dirty state tracking (`is_dirty`).
* Thread-safe latching (`std::mutex`) and page-level reader-writer locks (`std::shared_mutex`).
* Enforces the Write-Ahead Logging rule: before writing any dirty page to disk, invokes `LogManager::FlushLogBufferUpTo(page->GetLSN())`.

### 3.3 Disk Manager
* Low-level block storage driver managing physical binary database files (`.db`).
* Provides page-level reads, writes, sequential page allocations, and OS file flushing (`FlushFileBuffers` on Windows, `fsync` on Linux).

### 3.4 B+ Tree Index
* Native multiway balanced search tree supporting logarithmic point lookups and sequential range iterations.
* Key-RID pairs stored exclusively in leaf nodes; internal nodes store routing keys and child page IDs.
* Leaf sibling pointers (`next_page_id`) enable linear range traversal (`ScanRange`).
* Automatic page splitting and tree height growth upon root splitting.

### 3.5 Transactions & Concurrency
* `TransactionManager` coordinates transaction IDs and lifecycle states (`ACTIVE`, `COMMITTED`, `ABORTED`).
* Undo logging (`TableWriteRecord`) records physical before-images.
* Transaction rollback reverses mutations and synchronizes secondary B+ tree index keys.

### 3.6 Write-Ahead Logging (WAL) & Crash Recovery
* `LogManager` manages a 64KB append buffer and sequential `.wal` disk log with monotonic 64-bit Log Sequence Numbers (`LSN`).
* `RecoveryManager` executes three-pass ARIES-style recovery on unclean shutdown:
  1. **Analysis Pass**: Identifies uncommitted loser transactions and highest LSN.
  2. **Redo Pass**: Replays all committed DML records forward to bring physical pages to the crash point.
  3. **Undo Pass**: Traverses uncommitted loser transactions backward in reverse LSN order, undoing modifications and restoring original table states.
