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

## Known Limitations

* Educational Scope: Built for systems understanding, correctness, and technical interviews rather than cloud-scale production.
* Outer Joins: Supports `INNER JOIN` and `LEFT JOIN`; `RIGHT JOIN` and `FULL OUTER JOIN` are not implemented.
* Single-Node: Standalone embedded/local server architecture without distributed clustering or consensus.

---

## License

Educational and Open Source under the MIT License.
