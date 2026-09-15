# ForgeDB Architecture Overview

ForgeDB is a from-scratch relational database management system designed with strict component separation across three primary layers: the **Interface Layer**, the **Relational Query Engine**, and the **Storage & Durability Engine**.

```
                         Browser (Vite/React)
                                 │
                                 │ HTTP REST
                                 ▼
                         ┌───────────────┐
                         │   Web API     │
                         └───────┬───────┘
                                 │
                 CLI ────────────┼──────────── Tests
                                 │
                                 ▼
                     ┌───────────────────────┐
                     │   ForgeDB Engine API  │
                     └───────────┬───────────┘
                                 │
            ┌────────────────────┼────────────────────┐
            ▼                    ▼                    ▼
       SQL Parser          Query Planner           Catalog
      (Tokens/AST)        (Scan/Filter/Join)     (Tables/Columns)
            │                    │                    │
            └────────────────────┼────────────────────┘
                                 ▼
                           Query Executor
                    (Volcano / Iterator Pattern)
                                 │
            ┌────────────────────┼────────────────────┐
            ▼                    ▼                    ▼
        Seq Scan             Join Ops             Index Scan
       (TableHeap)       (NestedLoopJoin)         (B+ Tree)
            │                    │                    │
            └────────────────────┼────────────────────┘
                                 ▼
                          Storage Engine
                                 │
            ┌────────────────────┼────────────────────┐
            ▼                    ▼                    ▼
       Slotted Pages        Buffer Pool            B+ Tree
       (4096-byte)         (LRU Eviction)         (Indexes)
            │                    │                    │
            └────────────────────┼────────────────────┘
                                 ▼
                            Disk Manager
                          (File I/O, Paging)
                                 │
                                 ▼
                        Write-Ahead Logging
                       (LogManager, LSNs)
                                 │
                                 ▼
                          Crash Recovery
                       (ARIES Redo/Undo)
```

## 1. Interface Layer
- **CLI Shell**: Direct interactive REPL for issuing SQL and meta-commands (`.help`, `.schema`, `.tables`, `.exit`).
- **HTTP REST API**: Exposes endpoints (`/api/query`, `/api/tables`, `/api/health`) enabling the browser UI to interact directly with the engine.
- **Web UI**: Minimal React/Vite SQL editor and results visualizer.

## 2. Relational Query Engine
- **Lexer & Parser**: Converts raw SQL into a typed Abstract Syntax Tree (AST) without external dependencies (no Flex/Bison, no third-party SQL parsers).
- **Catalog**: Persisted system metadata storing table definitions, schemas, column data types, and index bindings.
- **Planner**: Generates physical operator trees from the AST, choosing between sequential scans and index scans.
- **Executor**: Volcano-style iterator model (`Init()`, `Next()`, `Close()`) with operators: `SeqScan`, `IndexScan`, `Filter`, `Projection`, `Sort`, `Limit`, `Aggregate`, `GroupBy`, `NestedLoopJoin`, `Insert`, `Update`, `Delete`.

## 3. Storage & Durability Engine
- **Slotted Pages**: Fixed 4096-byte pages with header metadata and slot directories supporting variable-length records.
- **Buffer Pool Manager**: Frames cached in memory, tracked via dirty flags and pin counts, with LRU replacement.
- **Disk Manager**: Page allocation, block-level file reads/writes, flush control.
- **B+ Tree Index**: Native multiway search tree supporting logarithmic point searches and range scans over leaf siblings.
- **Concurrency & Transactions**: Transaction manager assigning transaction IDs, tracking status (`ACTIVE`, `COMMITTED`, `ABORTED`), and managing concurrency.
- **WAL & Recovery**: Write-Ahead Logging ensuring modifications are logged before dirty pages reach disk; crash recovery restoring consistency upon reboot.
