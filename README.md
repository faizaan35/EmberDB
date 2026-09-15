# EmberDB — From-Scratch Relational Database Engine

**EmberDB** is a lightweight, educational relational database management system built entirely from scratch in **C++17**.

No external database engines (SQLite, DuckDB, RocksDB, PostgreSQL, MySQL) are used. The storage engine, slotted pages, buffer pool manager, B+ Tree index, SQL lexer/parser, Volcano-style query executor, transaction manager, and Write-Ahead Logging (WAL) are implemented natively.

---

## Architecture Overview

```
SQL Query
   │
   ▼
[ Lexer & Parser ]  ──>  Abstract Syntax Tree (AST)
   │
   ▼
[ Query Planner ]   ──>  Physical Plan (SeqScan vs IndexScan)
   │
   ▼
[ Query Executor ]  ──>  Volcano-style Iterator Pipeline
   │
   ▼
[ Catalog & Tables] ──>  TableHeap & Schemas
   │
   ▼
[ Buffer Pool ]     ──>  LRU Frame Eviction & Pinning
   │
   ▼
[ Slotted Pages ]   ──>  4096-byte Page Layout & Records
   │
   ▼
[ Disk Manager ]    ──>  Block Storage & Filesystem I/O
   │
   ▼
[ WAL & Recovery ]  ──>  Crash Durability (ARIES Protocol)
```

---

## Supported SQL Scope

* **DDL**: `CREATE TABLE`, `DROP TABLE`, `CREATE INDEX`
* **DML**: `INSERT INTO`, `SELECT`, `UPDATE`, `DELETE`
* **Clauses**: `WHERE`, `ORDER BY (ASC/DESC)`, `LIMIT`
* **Data Types**: `INT`, `BIGINT`, `DOUBLE`, `BOOLEAN`, `VARCHAR`
* **Relational Joins**: `INNER JOIN`, `LEFT JOIN` via Nested Loop Join
* **Aggregations**: `COUNT(*)`, `SUM()`, `AVG()`, `MIN()`, `MAX()`, and `GROUP BY`
* **Transactions**: `BEGIN`, `COMMIT`, `ROLLBACK`

---

## Directory Structure

```text
EmberDB/
├── AGENTS.md               # Authoritative system specification
├── CMakeLists.txt          # Root CMake build configuration
├── README.md               # Project documentation
├── .gitignore              # Git ignore rules
│
├── docs/                   # Architectural & subsystem documentation
│   ├── progress.md         # Phase tracking and completion gate logs
│   ├── architecture.md     # Three-layer engine design
│   ├── storage-format.md   # Page & slotted record binary format
│   ├── sql.md              # Supported SQL syntax and semantics
│   ├── transactions.md     # Transaction states & concurrency
│   ├── recovery.md         # WAL records & ARIES recovery
│   └── design-decisions.md # Architecture Decision Records (ADRs)
│
├── src/                    # Database engine implementation
│   ├── main.cpp            # Interactive CLI executable entrypoint
│   ├── include/emberdb/    # Public engine headers
│   │   ├── common/         # Configuration, Status, Result types
│   │   └── emberdb.h       # Engine lifecycle interface
│   └── common/             # Common utility implementations
│
├── tests/                  # Test suites
│   ├── include/catch2/     # Catch2 test framework
│   ├── test_main.cpp       # Test runner entrypoint
│   └── unit/               # Subsystem unit tests
│
└── data/                   # Default storage directory (.gitkeep)
```

---

## Building and Running

### Prerequisites
* **C++17 Compiler**: GCC 9+, Clang 10+, or MSVC 2019+
* **Build System**: CMake 3.20+ and Ninja (or Make)

### 1. Configure
```powershell
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug
```

### 2. Build
```powershell
cmake --build build --config Debug
```

### 3. Run Tests
```powershell
ctest --test-dir build --output-on-failure
# or run the test binary directly:
.\build\bin\emberdb_tests.exe
```

### 4. Launch the Interactive CLI
```powershell
.\build\bin\emberdb.exe
```

Inside the CLI shell:
```text
EmberDB> .help
EmberDB> .version
EmberDB> .exit
```

---

## Known Limitations

* Educational scope: Designed for understanding database internals and technical interviews.
* Not intended for high-concurrency production workloads.
* No network clustering or distributed consensus.

---

## License

Educational and Open Source.
