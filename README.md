# EmberDB — Relational Database Management System from Scratch

[![Language](https://img.shields.io/badge/Language-C%2B%2B17-blue.svg)](https://en.cppreference.com/w/cpp/17)
[![Build System](https://img.shields.io/badge/Build-CMake%20%7C%20Ninja-orange.svg)](https://cmake.org/)
[![Testing](https://img.shields.io/badge/Tests-100%25%20Passed-brightgreen.svg)]()
[![Web Console](https://img.shields.io/badge/Frontend-React%2018%20%2B%20Vite-61dafb.svg)](https://react.dev/)

**EmberDB** is a lightweight, genuine relational database management system designed and implemented entirely from scratch in **C++17**.

This is a systems-engineering project, **not** a wrapper around SQLite, PostgreSQL, DuckDB, RocksDB, or BerkeleyDB. EmberDB implements its own disk persistence, fixed-size slotted pages, buffer pool manager, B+ Tree indexing, SQL tokenizer & recursive-descent parser, Volcano-style physical execution engine, ACID transactions, Write-Ahead Logging (WAL), ARIES-style crash recovery, native HTTP REST server, and React web console.

---

## Architecture Overview

```text
                             Browser (React 18 + Vite)
                                        │
                                        │ HTTP REST (JSON)
                                        ▼
                                ┌───────────────┐
                                │   HttpServer  │ (Native Winsock/POSIX)
                                └───────┬───────┘
                                        │
                         CLI ───────────┼─────────── Tests
                                        │
                                        ▼
                            ┌───────────────────────┐
                            │   EmberDBInstance     │ (Engine Lifecycle & Recovery)
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
                          (Volcano Iterator Model)
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

## "How EmberDB Works" — 10–15 Minute Technical Interview Guide

When explaining EmberDB in a technical interview, structure your explanation from top to bottom across the three foundational database layers:

### 1. Interface & SQL Compilation Layer (0 to 3 minutes)
* **SQL Lexing & Parsing**: When a query string (e.g. `SELECT users.name, orders.amount FROM users INNER JOIN orders ON users.id = orders.user_id WHERE orders.amount > 500;`) enters the engine via the CLI or HTTP API:
  - The hand-written **Lexer** transforms characters into strongly-typed tokens (keywords, identifiers, literals, operators).
  - The **Recursive-Descent Parser** constructs an Abstract Syntax Tree (AST) representing statements (`SelectStatement`, `InsertStatement`, `CreateTableStatement`, etc.) and expression trees (`BinaryOpExpression`, `ColumnValueExpression`).
* **Catalog & Query Planning**:
  - The **Catalog** validates table schemas, column data types, and checks for existing secondary indexes.
  - The **Query Planner** converts the AST into an optimal physical operator tree. If an equality or range predicate matches an indexed column (`WHERE id = 500`), the planner chooses an `IndexScanPlanNode` instead of a `SeqScanPlanNode`, attaching residual filter predicates where needed.

### 2. Query Execution & Operator Pipeline (3 to 6 minutes)
* **Volcano Iterator Model**: Operators evaluate lazily via standard `Init()`, `Next(Tuple* tuple, RID* rid)`, and `Close()` methods.
* **Operators**:
  - `SeqScanExecutor`: Iterates over the `TableHeap` page-by-page and slot-by-slot.
  - `IndexScanExecutor`: Uses the B+ Tree to search for matching `RID`s and fetches tuples directly from the buffer pool.
  - `FilterExecutor` & `ProjectionExecutor`: Evaluate expression trees against rows and project requested column sets.
  - `NestedLoopJoinExecutor`: Joins outer and inner tuple streams with support for both `INNER JOIN` and `LEFT JOIN` (emitting null-padded rows on unmatched left tuples).
  - `AggregateExecutor` & `GroupBy`: Computes grouped aggregates (`COUNT`, `SUM`, `AVG`, `MIN`, `MAX`) using in-memory hash buckets.

### 3. Storage Engine & Memory Hierarchy (6 to 9 minutes)
* **Slotted Pages (4096 bytes)**:
  - Fixed-size disk pages match the OS cluster size.
  - Page header contains page ID, LSN, next/previous page pointers, slot count, and free space pointer.
  - Variable-length records are placed from the bottom of the page upwards, while the slot directory grows downwards from the header. This allows records to be updated or compacted without modifying external `RID` pointers (`page_id`, `slot_id`).
* **Buffer Pool Manager**:
  - Maintains an in-memory frame array with a Least Recently Used (LRU) replacer.
  - Transparently manages page pin counts and dirty flags. Unpinned pages are evicted to make room for newly requested pages.

### 4. Indexing, Transactions & Durability (9 to 13 minutes)
* **B+ Tree Index**:
  - Native multi-way search tree where all data/RIDs are stored in leaf nodes and internal nodes guide search navigation.
  - Supports leaf sibling linking for fast range scans (`WHERE id >= 100 AND id <= 200`), automatic node splitting when pages fill up, and tree height growth upon root split.
* **ACID Transactions**:
  - Supports `BEGIN`, `COMMIT`, and `ROLLBACK`.
  - Transaction undo logs track table mutations. On rollback, table mutations are reversed (tombstoning inserts, rolling back updates with before-images, resurrecting deleted rows) and secondary indexes are synchronized.
* **Write-Ahead Logging (WAL) & ARIES Crash Recovery**:
  - Enforces the fundamental WAL rule: **Log first, data page later**. The `BufferPoolManager` flushes WAL records up to a page's LSN before writing that dirty page to disk.
  - `RecoveryManager` executes three-pass ARIES recovery upon detecting an unclean shutdown:
    1. **Analysis Pass**: Reconstructs active transactions and highest LSN.
    2. **Redo Pass (Repeating History)**: Replays all committed DML records forward to bring pages up to the crash point.
    3. **Undo Pass (Rolling Back Losers)**: Rolls back all active/uncommitted transactions backward in reverse LSN order.

---

## Key Algorithms

### 1. Slotted Page Insert & Compaction
* When inserting a record of size $S$:
  - Verify contiguous free space $\ge S + \text{sizeof(Slot)}$.
  - Decrement `free_space_pointer` by $S$, copy record payload into that offset.
  - Append a new `Slot{offset, length}` to the slot directory, or reuse an existing tombstoned slot.
* Upon record deletion:
  - Set `offset = 0, length = 0` (tombstone). Other slot indices remain stable, preserving `RID` references.

### 2. Buffer Pool LRU Eviction
* Each frame tracks a `pin_count` and `is_dirty` bit.
* Only unpinned frames (`pin_count == 0`) are candidates in the LRU eviction list.
* Before evicting a dirty page, the buffer pool invokes `log_manager->FlushLogBufferUpTo(page->GetLSN())` to enforce WAL durability, then writes the 4KB block to disk via `DiskManager`.

### 3. B+ Tree Node Splitting
* When an insertion causes leaf entries to exceed maximum capacity:
  - Allocate a new leaf page from the buffer pool.
  - Move upper half of keys and `RID` pairs to the new leaf.
  - Update leaf sibling pointers (`next_page_id`).
  - Push the split key up to the parent internal page. If the parent exceeds capacity, split the internal node recursively. If the root splits, create a new root with height $+1$.

### 4. Nested Loop Join
* The outer child is iterated row-by-row.
* For each outer tuple, the inner child is scanned from the beginning, evaluating the join expression.
* In a `LEFT JOIN`, if no inner tuples match the outer tuple, a joined row is emitted with the right-hand attributes padded with `NULL` values.

---

## Quickstart

### Prerequisites
* **C++17 Compiler**: GCC 9+, Clang 10+, or MSVC 2019+
* **Build System**: CMake 3.20+ and Ninja (or Make)
* **Node.js**: Node 18+ and npm (for web console build)

### 1. Build the Database Engine
```powershell
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug
```

### 2. Run All Tests
```powershell
ctest --test-dir build --output-on-failure
# or run the Catch2 test runner directly:
.\build\bin\emberdb_tests.exe
```

### 3. Launch the Interactive CLI REPL
```powershell
.\build\bin\emberdb.exe
```
Inside the shell:
```text
========================================
EmberDB v0.1.0 - Relational Database Engine
Type '.help' for commands or '.exit' to quit.
========================================
EmberDB> CREATE TABLE users (id INT, name VARCHAR, age INT);
Query OK, 0 row(s) affected.

EmberDB> INSERT INTO users VALUES (1, 'Faizaan', 23);
Query OK, 1 row(s) affected.

EmberDB> SELECT * FROM users;
+----+---------+-----+
| id | name    | age |
+----+---------+-----+
| 1  | Faizaan | 23  |
+----+---------+-----+
1 row(s) in set.

EmberDB> .tables
users

EmberDB> .schema users
Table: users
----------------------------------------
id                  INTEGER
name                VARCHAR
age                 INTEGER

EmberDB> .exit
```

### 4. Run Headless / Non-Interactive SQL Scripts
```powershell
.\build\bin\emberdb.exe -c "CREATE TABLE demo (x INT); INSERT INTO demo VALUES (42); SELECT * FROM demo;"
```

### 5. Launch the HTTP API Server & Web Console
Build the web UI (one-time):
```powershell
cd web
npm install
npm run build
cd ..
```

Start the EmberDB HTTP server:
```powershell
.\build\bin\emberdb_server.exe --port 8080 --data-dir data
```

Open `http://localhost:8080/` in any browser to use the **EmberDB Web SQL Console**!

---

## HTTP REST API

| Method | Endpoint | Description |
|---|---|---|
| `GET` | `/` | Serves the built React Web SQL Console UI |
| `GET` | `/api/health` | Engine health status, version, and server readiness |
| `GET` | `/api/tables` | List of all persisted catalog tables |
| `GET` | `/api/schema/:table` | Column definitions and types for a specific table |
| `POST` | `/api/query` | Execute SQL statement (`{"sql": "..."}`) and return rows, columns, and timings |
| `OPTIONS` | `/api/*` | CORS preflight handler for web browser clients |

---

## Project Phase Summary

| Phase | Description | Status | Completion Gate |
|---|---|:---:|:---:|
| **0** | Environment Setup & CMake Scaffold | **COMPLETE** | CMake compiles, test runner passes |
| **1** | Page & Disk Manager (4096B blocks) | **COMPLETE** | Write/read/allocate pages, block persistence |
| **2** | Records, Slotted Pages, Catalog | **COMPLETE** | Multi-table persistence across restarts |
| **3** | SQL Lexer & Recursive-Descent Parser | **COMPLETE** | Full SQL tokenization & AST generation |
| **4** | Volcano Query Execution Engine | **COMPLETE** | `CREATE`, `INSERT`, `SELECT`, `UPDATE`, `DELETE` |
| **5** | Query Features (ORDER BY, LIMIT, AGG) | **COMPLETE** | `COUNT`, `SUM`, `AVG`, `MIN`, `MAX`, `GROUP BY` |
| **6** | Relational Joins (INNER, LEFT) | **COMPLETE** | Nested Loop Join with predicates & projection |
| **7** | Buffer Pool Manager (LRU Replacer) | **COMPLETE** | Page pinning, dirty eviction, small-pool stress |
| **8** | Native B+ Tree Secondary Index | **COMPLETE** | 1k+ inserts, node splitting, range scans |
| **9** | Query Planner & Index Scan Selection | **COMPLETE** | Automatic `IndexScan` selection & residual filters |
| **10** | ACID Transactions | **COMPLETE** | `BEGIN`, `COMMIT`, `ROLLBACK`, undo logging |
| **11** | Concurrency Primitives | **COMPLETE** | Thread-safe BPM, Catalog, Table, B+ Tree latches |
| **12** | Write-Ahead Logging (WAL) | **COMPLETE** | 64KB log buffer, LSNs, log-first-data-later rule |
| **13** | Crash Recovery (ARIES Protocol) | **COMPLETE** | Unclean shutdown detection, Redo pass, Undo pass |
| **14** | CLI Shell Polish | **COMPLETE** | Multi-line input, ASCII tables, meta-commands |
| **15** | Native C++ HTTP REST API | **COMPLETE** | Native sockets, JSON serialization, endpoints |
| **16** | React Web SQL Console | **COMPLETE** | Vite + React UI, schema inspection, history |
| **17** | System Integration & Hardening | **COMPLETE** | Multi-table joins, crash recovery under load, stress |
| **18** | Documentation & Interview Readiness | **COMPLETE** | Comprehensive architecture, formats, & guides |

---

## Known Limitations

* Educational Scope: Built for systems understanding, correctness, and technical interviews rather than cloud-scale production.
* Outer Joins: Supports `INNER JOIN` and `LEFT JOIN`; `RIGHT JOIN` and `FULL OUTER JOIN` are not implemented.
* Single-Node: Standalone embedded/local server architecture without distributed clustering or consensus.

---

## License

Educational and Open Source under the MIT License.
