# AGENTS.md

# EmberDB — Database From Scratch

## 0. Mission

You are building **EmberDB**, a small but genuine relational database management system from scratch.

This is a **systems-engineering project**, not a CRUD application that happens to use a database.

The goal is to implement the important internal components of a relational database ourselves:

- SQL tokenizer
- SQL parser
- Abstract Syntax Tree / query representation
- Catalog
- Query planner
- Query executor
- Table storage
- Record storage
- Fixed-size pages
- Page manager
- Buffer pool
- Disk persistence
- B+ Tree indexes
- Sequential table scans
- Expressions
- Filtering
- Sorting
- Aggregations
- GROUP BY
- JOIN execution
- Transactions
- Commit / rollback
- Concurrency primitives
- Write-Ahead Logging
- Crash recovery
- Basic query optimization
- HTTP API
- Basic web SQL console

The final result should be a working local database that can accept SQL, execute it through our own database engine, persist data to disk, and expose results through a simple browser interface.

The project must be understandable and defensible in a technical interview.

Do NOT attempt to reproduce the complete functionality of PostgreSQL, MySQL, SQLite, or DuckDB.

The target is a **small educational/engineering database**, not a production database.

---

# 1. Non-Negotiable Rules

These rules apply throughout the entire project.

## 1.1 The database engine must be ours

Do NOT use an existing database engine for the core database functionality.

Absolutely do NOT use:

- SQLite
- PostgreSQL
- MySQL
- MariaDB
- MongoDB
- DuckDB
- H2
- Derby
- Apache Calcite as the execution engine
- Any embedded SQL database
- Any external database for storing EmberDB tables

The database engine must store and retrieve its own data.

We may use standard C++ libraries and small infrastructure libraries where appropriate.

---

## 1.2 Do not fake database functionality

Do not implement something like:

```text
SQL → parse → translate to SQLite query → return SQLite result
```

That is explicitly forbidden.

Likewise, do not:

- store tables as JSON and call that a storage engine
- store all records in one giant vector permanently
- load the entire database into memory and pretend it is disk storage
- use an external B+ tree implementation
- use an external SQL parser
- use an external query engine

The important database internals must be implemented by us.

---

## 1.3 C++17 is the core language

The database engine must be implemented in:

**C++17**

Use:

- STL
- smart pointers
- RAII
- `std::vector`
- `std::string`
- `std::unordered_map`
- `std::map`
- `std::optional`
- `std::variant`
- `std::unique_ptr`
- `std::shared_ptr` only when genuinely necessary
- `std::mutex`
- `std::shared_mutex`
- filesystem APIs
- standard file I/O

Avoid raw `new` / `delete` unless there is a very strong reason.

Prefer:

```cpp
std::unique_ptr
```

over manual ownership.

---

# 2. Project Philosophy

The most important principle is:

> Correctness first. Performance second. Complexity last.

Do not prematurely optimize.

First implement:

```text
Correct
    ↓
Tested
    ↓
Persistent
    ↓
Indexed
    ↓
Concurrent
    ↓
Optimized
```

Do not implement advanced query optimization before the underlying execution engine works.

Do not implement WAL before transactions exist.

Do not implement transactions before the storage layer is reliable.

Do not implement B+ Tree indexing before sequential scans work correctly.

---

# 3. Target Architecture

The final project should approximately follow this architecture:

```text
                         Browser
                            │
                            │ HTTP
                            ▼
                    ┌─────────────────┐
                    │   Web API       │
                    │    Layer        │
                    └────────┬────────┘
                             │
                             ▼
                    ┌─────────────────┐
                    │ Database Engine  │
                    │     API          │
                    └────────┬────────┘
                             │
             ┌───────────────┼────────────────┐
             │               │                │
             ▼               ▼                ▼
        SQL Parser      Query Planner     Catalog
             │               │                │
             └───────────────┼────────────────┘
                             ▼
                     Query Executor
                             │
          ┌──────────────────┼──────────────────┐
          │                  │                  │
          ▼                  ▼                  ▼
      Seq Scan            Joins            Index Scan
          │                  │                  │
          └──────────────────┼──────────────────┘
                             ▼
                     Storage Engine
                             │
            ┌────────────────┼────────────────┐
            │                │                │
            ▼                ▼                ▼
       Page Manager      Buffer Pool       B+ Tree
            │                │             Indexes
            └────────────────┼────────────────┘
                             ▼
                       Disk Storage
                             │
                             ▼
                            WAL
                             │
                             ▼
                     Crash Recovery
```

---

# 4. Repository Structure

Create and maintain a clean project structure.

A reasonable target structure is:

```text
EmberDB/
│
├── AGENTS.md
├── README.md
├── CMakeLists.txt
├── .gitignore
│
├── docs/
│   ├── architecture.md
│   ├── storage-format.md
│   ├── sql.md
│   ├── transactions.md
│   ├── recovery.md
│   └── design-decisions.md
│
├── src/
│   ├── main.cpp
│   │
│   ├── common/
│   │   ├── types/
│   │   ├── status/
│   │   ├── exceptions/
│   │   └── utilities/
│   │
│   ├── catalog/
│   │   ├── catalog.cpp
│   │   ├── catalog.h
│   │   ├── schema.cpp
│   │   ├── schema.h
│   │   └── column.cpp
│   │
│   ├── storage/
│   │   ├── page/
│   │   ├── disk/
│   │   ├── record/
│   │   ├── table/
│   │   └── buffer/
│   │
│   ├── index/
│   │   └── btree/
│   │
│   ├── sql/
│   │   ├── lexer/
│   │   ├── parser/
│   │   └── ast/
│   │
│   ├── execution/
│   │   ├── operators/
│   │   ├── expressions/
│   │   ├── executor/
│   │   └── joins/
│   │
│   ├── planner/
│   │   ├── planner.cpp
│   │   ├── planner.h
│   │   └── optimizer/
│   │
│   ├── transaction/
│   │   ├── transaction.cpp
│   │   ├── transaction_manager.cpp
│   │   └── lock_manager.cpp
│   │
│   ├── recovery/
│   │   ├── wal.cpp
│   │   ├── log_manager.cpp
│   │   └── recovery_manager.cpp
│   │
│   └── server/
│       ├── http_server.cpp
│       ├── api.cpp
│       └── routes.cpp
│
├── tests/
│   ├── unit/
│   ├── integration/
│   ├── sql/
│   ├── storage/
│   ├── index/
│   ├── transaction/
│   └── recovery/
│
├── web/
│   ├── package.json
│   ├── src/
│   └── ...
│
├── data/
│   └── .gitkeep
│
└── scripts/
```

The exact structure may evolve as implementation progresses.

Do not create dozens of empty files before they are needed.

---

# 5. Technology Stack

## Core

```text
Language: C++17
Build: CMake
Compiler: MSVC / GCC / Clang
Testing: GoogleTest or Catch2
```

The core database must remain portable across Windows and Linux as much as reasonably practical.

The primary development environment is Windows.

---

## Web API

The API must eventually be implemented in C++.

A lightweight HTTP library may be used for networking.

The HTTP library is infrastructure only.

It must NOT provide:

- SQL parsing
- SQL execution
- database storage
- indexing
- transactions
- query planning

Those remain our implementation.

---

## Frontend

Use:

```text
React
Vite
JavaScript or TypeScript
```

The frontend should be extremely simple.

This is NOT a frontend project.

The UI only needs:

```text
EmberDB

┌──────────────────────────────────────────────┐
│ SELECT * FROM users;                         │
│                                              │
│                                              │
└──────────────────────────────────────────────┘

[ Execute ]

Results:

┌────┬──────────┬───────┐
│ id │ name     │ age   │
├────┼──────────┼───────┤
│ 1  │ Faizaan  │ 23    │
│ 2  │ Ahmed    │ 24    │
└────┴──────────┴───────┘
```

Additional simple features:

- query history
- execution time
- row count
- error display
- database/table list

No authentication is necessary for the local development version.

No fancy CSS is necessary.

---

# 6. Supported SQL Scope

The SQL implementation must be deliberately limited.

Document the supported SQL grammar.

## 6.1 Data Definition

Required:

```sql
CREATE TABLE users (
    id INT,
    name VARCHAR,
    age INT
);
```

Eventually support:

```sql
DROP TABLE users;
```

Optional:

```sql
ALTER TABLE
```

Do NOT implement ALTER TABLE unless the rest of the system is already stable.

---

# 7. Data Types

Initial supported types:

```text
INT
BIGINT
DOUBLE
BOOLEAN
VARCHAR
```

Optional later:

```text
FLOAT
TEXT
```

Do not add many types unnecessarily.

Each value must have an explicit internal representation.

A useful design is a tagged value type such as:

```cpp
std::variant<
    int32_t,
    int64_t,
    double,
    bool,
    std::string,
    std::monostate
>
```

The design may differ if a better approach is justified.

---

# 8. INSERT

Required:

```sql
INSERT INTO users
VALUES (1, 'Faizaan', 23);
```

Eventually support:

```sql
INSERT INTO users (id, name, age)
VALUES (1, 'Faizaan', 23);
```

Validate:

- table existence
- column count
- data types
- column order
- invalid values

---

# 9. SELECT

Required:

```sql
SELECT * FROM users;
```

Required:

```sql
SELECT id, name FROM users;
```

Required:

```sql
SELECT name FROM users
WHERE age > 20;
```

Supported operators should include:

```text
=
!=
<
<=
>
>=
```

Logical operators:

```text
AND
OR
NOT
```

---

# 10. UPDATE

Required:

```sql
UPDATE users
SET age = 24
WHERE id = 1;
```

Support multiple assignments where practical:

```sql
UPDATE users
SET age = 24,
    name = 'Ahmed'
WHERE id = 1;
```

---

# 11. DELETE

Required:

```sql
DELETE FROM users
WHERE id = 1;
```

Also support:

```sql
DELETE FROM users;
```

provided the semantics are clearly documented.

---

# 12. ORDER BY

Required:

```sql
SELECT *
FROM users
ORDER BY age;
```

Support:

```sql
ASC
DESC
```

Example:

```sql
SELECT *
FROM users
ORDER BY age DESC;
```

---

# 13. LIMIT

Required:

```sql
SELECT *
FROM users
LIMIT 10;
```

Eventually:

```sql
SELECT *
FROM users
ORDER BY age DESC
LIMIT 10;
```

---

# 14. Expressions

The query engine must eventually support expressions such as:

```sql
age + 1
```

and comparisons such as:

```sql
age > 20
```

Support arithmetic where practical:

```text
+
-
*
/
```

Expressions should be represented in the AST rather than evaluated directly inside the parser.

---

# 15. Aggregations

Required aggregate functions:

```text
COUNT
SUM
AVG
MIN
MAX
```

Examples:

```sql
SELECT COUNT(*) FROM users;
```

```sql
SELECT AVG(age) FROM users;
```

---

# 16. GROUP BY

Required:

```sql
SELECT age, COUNT(*)
FROM users
GROUP BY age;
```

Implement grouping in the execution engine.

Do not fake GROUP BY by running multiple independent SELECT statements.

---

# 17. JOINs

JOINs are a required part of the final project.

At minimum implement:

```text
INNER JOIN
LEFT JOIN
```

Syntax:

```sql
SELECT *
FROM users
INNER JOIN orders
ON users.id = orders.user_id;
```

Also:

```sql
SELECT *
FROM users
LEFT JOIN orders
ON users.id = orders.user_id;
```

Multiple table queries should eventually work:

```sql
SELECT users.name, orders.amount
FROM users
INNER JOIN orders
ON users.id = orders.user_id
WHERE orders.amount > 1000;
```

---

# 18. JOIN IMPLEMENTATION STRATEGY

The first JOIN algorithm must be:

**Nested Loop Join**

Conceptually:

```text
for each row in left table:
    for each row in right table:
        evaluate join condition
        if condition is true:
            emit combined row
```

This is intentionally simple.

Do NOT begin with advanced join algorithms.

Later, after indexing and correctness are established, optionally implement:

```text
Hash Join
Index Nested Loop Join
```

Only implement these after the basic Nested Loop Join passes all tests.

---

# 19. Query Execution Architecture

Use an operator-based execution model where practical.

Potential operators:

```text
SeqScan
IndexScan
Filter
Projection
Sort
Limit
Aggregate
GroupBy
NestedLoopJoin
Insert
Update
Delete
```

A query should conceptually become an execution tree:

```text
SELECT users.name
FROM users
WHERE users.age > 20;
```

could become:

```text
Projection
    │
    ▼
Filter(age > 20)
    │
    ▼
SeqScan(users)
```

A JOIN:

```text
Projection
    │
    ▼
Filter
    │
    ▼
NestedLoopJoin
   /       \
SeqScan   SeqScan
users     orders
```

The exact class hierarchy is up to the implementation, but it must remain understandable.

---

# 20. Storage Engine

This is one of the most important parts of the project.

The database must persist data to disk.

Do NOT simply serialize the entire database into JSON.

The database should use pages.

---

# 21. Page System

Use a fixed page size.

Recommended:

```text
4096 bytes
```

Each page should have a page identifier.

Conceptually:

```text
Page
├── Page ID
├── Header
├── Metadata
└── Data
```

Pages must be readable/writable from disk.

The exact header layout should be documented.

---

# 22. Disk Manager

Implement a disk manager responsible for:

```text
open database file
read page
write page
allocate new page
flush
close
```

Example conceptual API:

```cpp
PageId allocatePage();

void readPage(
    PageId pageId,
    char* destination
);

void writePage(
    PageId pageId,
    const char* source
);
```

The actual API may differ.

The DiskManager must not understand SQL.

It only understands pages and files.

---

# 23. Record Manager

Implement record storage inside pages.

Responsibilities:

- serialize records
- deserialize records
- insert records
- locate records
- update records
- delete records
- manage record identifiers

Use a record identifier such as:

```text
RID
├── page_id
└── slot_id
```

This should allow a row to be identified without storing the entire row location everywhere.

---

# 24. Slotted Pages

Prefer a slotted-page design for variable-length records.

Conceptually:

```text
┌──────────────────────────┐
│ Page Header              │
├──────────────────────────┤
│ Slot Directory           │
├──────────────────────────┤
│                          │
│ Record                   │
│ Record                   │
│ Record                   │
│                          │
└──────────────────────────┘
```

This allows records to move within the page while their slot identity remains stable.

The exact layout must be documented in:

```text
docs/storage-format.md
```

---

# 25. Buffer Pool

Implement a buffer pool.

Responsibilities:

- load pages from disk
- keep pages in memory
- pin/unpin pages
- track dirty pages
- flush dirty pages
- evict pages when the pool is full

Initially use a simple replacement strategy such as:

```text
LRU
```

Do not over-engineer it.

The buffer pool must be independent of SQL.

---

# 26. Catalog

The database needs metadata about:

```text
tables
columns
types
indexes
```

The catalog should allow the query engine to answer questions such as:

```text
Does table users exist?
What columns does users have?
What is the type of users.age?
Does users have an index on id?
```

The catalog itself must be persisted.

Do not hard-code table definitions into the C++ source.

---

# 27. SQL Lexer

Implement a tokenizer/lexer.

It should identify:

```text
keywords
identifiers
numbers
strings
operators
punctuation
parentheses
commas
semicolon
```

Example:

```sql
SELECT name FROM users WHERE age > 20;
```

should become tokens resembling:

```text
SELECT
IDENTIFIER(name)
FROM
IDENTIFIER(users)
WHERE
IDENTIFIER(age)
GREATER_THAN
NUMBER(20)
SEMICOLON
```

The lexer should not execute anything.

---

# 28. SQL Parser

Implement a parser that converts tokens into an AST/query representation.

The parser should handle syntax errors gracefully.

Example:

```sql
SELECT name FROM users WHERE age > 20;
```

becomes approximately:

```text
SelectStatement
├── columns
│   └── name
├── table
│   └── users
└── where
    └── age > 20
```

The parser must not access disk directly.

Keep parsing separate from execution.

---

# 29. B+ Tree Index

A B+ Tree is a required major subsystem.

Implement our own B+ Tree.

Do NOT use an existing database index implementation.

Required:

```text
search
insert
node splitting
leaf nodes
internal nodes
key comparison
RID storage
```

Eventually support:

```text
range scan
```

Example:

```sql
SELECT *
FROM users
WHERE id = 50;
```

If an index exists on `id`, the planner should eventually choose:

```text
IndexScan
```

rather than:

```text
SeqScan
```

---

# 30. B+ Tree Requirements

The implementation should include:

```text
Internal nodes
Leaf nodes
Parent relationships or equivalent navigation
Sibling pointers between leaf nodes
Splitting
Root creation
Root splitting
Search
Insertion
Range iteration
```

Deletion from the B+ Tree is NOT required in the first indexing version.

If deletion is implemented later, it must be tested thoroughly before being considered complete.

---

# 31. Query Planner

The planner converts parsed SQL into an execution plan.

Initially the planner can be simple.

Example:

```sql
SELECT *
FROM users
WHERE id = 10;
```

If there is no index:

```text
SeqScan(users)
→ Filter(id = 10)
```

If an index exists:

```text
IndexScan(users.id, 10)
```

The planner must be able to inspect catalog metadata.

---

# 32. Basic Query Optimization

Do not build a sophisticated optimizer.

Implement only useful basic optimizations.

Examples:

```text
Use index when appropriate
Push filters closer to scans
Avoid unnecessary work
```

Later, optionally implement:

```text
Join ordering
Cost estimation
Hash joins
```

These are stretch goals.

---

# 33. Transactions

Transactions are required.

At minimum:

```sql
BEGIN;
```

```sql
COMMIT;
```

```sql
ROLLBACK;
```

Example:

```sql
BEGIN;

UPDATE accounts
SET balance = balance - 100
WHERE id = 1;

UPDATE accounts
SET balance = balance + 100
WHERE id = 2;

COMMIT;
```

Rollback:

```sql
BEGIN;

DELETE FROM users
WHERE id = 10;

ROLLBACK;
```

After rollback, the deleted row must be restored.

---

# 34. Transaction Manager

Implement a transaction manager responsible for:

```text
transaction IDs
transaction state
BEGIN
COMMIT
ABORT/ROLLBACK
```

States can conceptually include:

```text
ACTIVE
COMMITTED
ABORTED
```

The exact implementation can differ.

---

# 35. Concurrency

Concurrency is part of the final project.

At minimum, protect shared structures such as:

```text
buffer pool
catalog
transaction manager
page structures
```

using appropriate synchronization.

Use:

```cpp
std::mutex
std::shared_mutex
```

where appropriate.

Do not claim full database isolation guarantees unless they are actually implemented and tested.

The first target is safe concurrent access to database internals.

---

# 36. Write-Ahead Logging

Implement WAL after transactions work.

Core principle:

> Log changes before flushing the corresponding modified database pages to disk.

The WAL subsystem should support enough information to recover committed changes after a crash.

A conceptual log record can contain:

```text
LSN
Transaction ID
Operation
Page ID
Record information
Before image / After image
```

The exact format is up to the implementation.

Document it.

---

# 37. Recovery

Implement basic crash recovery.

The database should be able to:

```text
start
detect unclean shutdown
read WAL
recover committed changes
discard/undo incomplete transaction changes where required
continue operating
```

Create explicit recovery tests.

Do NOT claim production-grade recovery.

This is an educational implementation.

---

# 38. HTTP API

Expose the database through HTTP.

At minimum:

```http
POST /api/query
```

Request:

```json
{
  "sql": "SELECT * FROM users;"
}
```

Response:

```json
{
  "success": true,
  "columns": ["id", "name", "age"],
  "rows": [
    [1, "Faizaan", 23]
  ],
  "executionTimeMs": 2
}
```

Errors should return useful information:

```json
{
  "success": false,
  "error": "Table 'users' does not exist"
}
```

Additional useful endpoints:

```text
GET /api/tables
GET /api/schema/:table
GET /api/health
```

The API must call the actual EmberDB engine.

It must never use another database.

---

# 39. Frontend

Create a minimal React/Vite application.

Required:

### SQL Editor

Large textarea/editor:

```text
SELECT * FROM users;
```

### Execute button

Calls:

```text
POST /api/query
```

### Results table

Displays:

```text
columns
rows
```

### Error display

Displays database errors.

### Metadata panel

Show:

```text
Tables
Schema
```

### Query history

Store recent queries in browser memory/localStorage.

The frontend is only a testing interface.

Do not spend significant development time on visual design.

---

# 40. CLI

Before the web interface is implemented, EmberDB must have a CLI.

Example:

```text
EmberDB> CREATE TABLE users (id INT, name VARCHAR);
Query OK

EmberDB> INSERT INTO users VALUES (1, 'Faizaan');
Query OK

EmberDB> SELECT * FROM users;

id | name
---+--------
1  | Faizaan

1 row returned.
```

The CLI is extremely important because it allows the database core to be tested independently of the web application.

The CLI must talk directly to the database engine.

---

# 41. Error Handling

Errors must be explicit and understandable.

Examples:

```text
Table does not exist
Column does not exist
Invalid SQL syntax
Type mismatch
Duplicate object
Invalid JOIN condition
Invalid transaction state
Page not found
Corrupted page
Database file error
```

Do not silently ignore errors.

Do not use:

```cpp
catch (...) {}
```

without a legitimate reason.

---

# 42. Testing Philosophy

Testing is a first-class part of the project.

Every major subsystem must have tests.

Do not consider a phase complete because:

```text
"the code compiles"
```

A phase is complete only when:

```text
Implementation
+
Unit Tests
+
Integration Tests
+
Manual Verification
+
Documentation
```

all pass.

---

# 43. Required Test Categories

## Unit tests

Test individual components:

```text
Lexer
Parser
Value
Page
DiskManager
Record
BufferPool
B+ Tree
Expressions
```

## Integration tests

Test combinations:

```text
Database + Disk
Database + Catalog
Parser + Executor
Executor + Storage
Planner + Index
Transactions + Storage
WAL + Recovery
```

## End-to-end tests

Example:

```text
CREATE TABLE
    ↓
INSERT
    ↓
SELECT
    ↓
UPDATE
    ↓
SELECT
    ↓
DELETE
    ↓
SELECT
```

---

# 44. Persistence Test

This is mandatory.

Test:

```text
process 1:
    CREATE TABLE
    INSERT rows
    exit

process 2:
    open same database

    SELECT rows
```

The rows must still exist.

If the data disappears after restarting EmberDB, the storage phase is not complete.

---

# 45. JOIN Test

Create:

```sql
CREATE TABLE users (
    id INT,
    name VARCHAR
);

CREATE TABLE orders (
    id INT,
    user_id INT,
    amount INT
);
```

Insert:

```sql
INSERT INTO users VALUES (1, 'A');
INSERT INTO users VALUES (2, 'B');

INSERT INTO orders VALUES (101, 1, 500);
INSERT INTO orders VALUES (102, 1, 1000);
INSERT INTO orders VALUES (103, 2, 200);
```

Test:

```sql
SELECT users.name, orders.amount
FROM users
INNER JOIN orders
ON users.id = orders.user_id;
```

Expected logical result:

```text
A | 500
A | 1000
B | 200
```

Also test:

```sql
SELECT users.name, orders.amount
FROM users
LEFT JOIN orders
ON users.id = orders.user_id;
```

including users that have no orders.

---

# 46. Transaction Test

Test:

```sql
BEGIN;

INSERT INTO users VALUES (10, 'Test');

ROLLBACK;
```

Then:

```sql
SELECT *
FROM users
WHERE id = 10;
```

Expected:

```text
0 rows
```

Commit test:

```sql
BEGIN;

INSERT INTO users VALUES (11, 'Committed');

COMMIT;
```

Restart the database.

The row must still exist.

---

# 47. WAL / Recovery Test

Simulate an unclean shutdown.

The test should:

1. Start database.
2. Begin transaction.
3. Modify data.
4. Write WAL.
5. Simulate process termination before normal shutdown.
6. Restart.
7. Run recovery.
8. Verify database consistency.

Do not rely only on theoretical recovery.

Create an automated test where reasonably possible.

---

# 48. Performance

Performance is NOT the primary goal.

However, avoid obviously catastrophic designs.

Do not repeatedly:

```text
read entire database file
deserialize every table
rewrite entire database
```

for every single SQL query.

The whole point of the storage engine and buffer pool is to avoid this.

Create a small benchmark suite later.

Potential benchmark:

```text
Insert 10,000 rows
Sequential scan 10,000 rows
Indexed lookup 10,000 rows
JOIN 10,000 rows
```

Record execution time.

---

# 49. Documentation

Every major subsystem must have documentation.

At minimum:

```text
README.md
docs/architecture.md
docs/storage-format.md
docs/sql.md
docs/transactions.md
docs/recovery.md
docs/design-decisions.md
```

The README must explain:

```text
What is EmberDB?
Why was it built?
Architecture
Supported SQL
How to build
How to run
How to run tests
How to use CLI
How to use web UI
Example queries
Known limitations
```

---

# 50. Design Decision Documentation

Whenever a major architectural decision is made, document:

```text
Decision
Why
Alternatives considered
Tradeoffs
```

Example:

```text
Decision:
Use a 4096-byte page.

Why:
Simple fixed-size page management and common database-engineering convention.

Tradeoffs:
Large rows may require special handling.
```

This documentation is important for future interviews.

---

# 51. Phase System

The entire implementation must be completed in the following phases.

**Do NOT skip phases.**

**Do NOT start a later phase while the previous phase is incomplete.**

Each phase has a gate.

A phase is complete only when its gate passes.

---

# PHASE 0 — Project Foundation

## Objectives

Set up:

```text
C++17
CMake
Git
Testing framework
Project structure
Build configuration
```

Create:

```text
README.md
docs/
src/
tests/
```

Create a minimal executable:

```text
EmberDB
```

It should start successfully.

Create a test executable.

### Deliverables

- CMake configuration
- Debug build
- Release build
- Test command
- Basic project documentation
- `.gitignore`

### Gate

Must pass:

```text
cmake configure
cmake build
all tests
```

No Phase 1 work until this passes.

---

# PHASE 1 — Basic Types + Pages + Disk Manager

Implement:

```text
PageId
RID
Value
Column
Schema
Page
DiskManager
```

Implement:

```text
allocate page
write page
read page
flush
```

### Tests

- write page
- read page
- persistence after process restart
- page allocation
- page IDs
- serialization

### Gate

A test must prove:

```text
write page
→ close process
→ reopen database
→ read page
→ exact same contents
```

Only after this passes move to Phase 2.

---

# PHASE 2 — Records + Tables + Catalog

Implement:

```text
Record
SlottedPage
TableHeap
Table
Catalog
```

Support:

```text
insert record
read record
update record
delete record
scan table
```

Implement table metadata persistence.

### Gate

This must work without SQL:

```text
create table
insert records
close database
reopen
scan table
```

All data must survive restart.

---

# PHASE 3 — SQL Lexer + Parser

Implement:

```text
Lexer
Tokens
Parser
AST / statements
```

Support parsing:

```text
CREATE TABLE
INSERT
SELECT
UPDATE
DELETE
WHERE
ORDER BY
LIMIT
```

At this phase parsing is the focus.

The parser does not need to execute queries yet.

### Gate

Every supported SQL statement must parse correctly.

Invalid SQL must produce useful errors.

Do not proceed until parser tests pass.

---

# PHASE 4 — Basic Query Execution

Connect:

```text
Parser
    ↓
Executor
    ↓
Catalog
    ↓
Storage
```

Implement:

```text
CREATE TABLE
INSERT
SELECT
UPDATE
DELETE
```

Implement:

```text
WHERE
projection
```

### Gate

The CLI must successfully execute:

```sql
CREATE TABLE users (
    id INT,
    name VARCHAR,
    age INT
);

INSERT INTO users VALUES (1, 'Faizaan', 23);

SELECT * FROM users;

SELECT name
FROM users
WHERE age > 20;

UPDATE users
SET age = 24
WHERE id = 1;

DELETE FROM users
WHERE id = 1;
```

All results must be correct.

---

# PHASE 5 — Query Features

Implement:

```text
ORDER BY
ASC/DESC
LIMIT
expressions
COUNT
SUM
AVG
MIN
MAX
GROUP BY
```

### Gate

Automated SQL tests must validate all features.

Example:

```sql
SELECT age, COUNT(*)
FROM users
GROUP BY age
ORDER BY age DESC
LIMIT 5;
```

---

# PHASE 6 — JOINs

Implement:

```text
INNER JOIN
LEFT JOIN
JOIN ... ON
multiple-table queries
```

First algorithm:

```text
Nested Loop Join
```

Do not implement hash join yet.

### Gate

All JOIN tests must pass.

Must test:

```text
matching rows
no matching rows
multiple matches
LEFT JOIN unmatched rows
JOIN + WHERE
JOIN + projection
JOIN + ORDER BY
JOIN + LIMIT
```

Only after this phase passes may indexing begin.

---

# PHASE 7 — Buffer Pool

Implement:

```text
BufferPool
PageFrame
pin/unpin
dirty tracking
eviction
LRU replacement
```

Integrate with storage.

### Gate

The database must remain correct with a buffer pool smaller than the total number of database pages.

Test:

```text
load pages
evict pages
modify pages
flush pages
reload pages
```

No data corruption is allowed.

---

# PHASE 8 — B+ Tree Index

Implement our own:

```text
B+ Tree
InternalNode
LeafNode
Search
Insert
Split
Leaf sibling links
Range scan
```

Add:

```sql
CREATE INDEX
```

if the design requires explicit index creation.

Example:

```sql
CREATE INDEX idx_users_id
ON users(id);
```

### Gate

Test:

```text
1 insertion
100 insertions
1000 insertions
node splitting
root splitting
search
range scan
persistence
```

All tests must pass.

---

# PHASE 9 — Query Planner + Index Scan

Implement:

```text
QueryPlan
SeqScan
IndexScan
basic optimizer
```

Planner should choose index scans where appropriate.

Example:

```sql
SELECT *
FROM users
WHERE id = 500;
```

with:

```text
users.id index
```

should use:

```text
IndexScan
```

when the planner determines it is appropriate.

### Gate

Tests must verify:

```text
correct result
correct plan
fallback to sequential scan
```

---

# PHASE 10 — Transactions

Implement:

```text
Transaction
TransactionManager
BEGIN
COMMIT
ROLLBACK
```

Connect transactions to storage.

### Gate

Must pass:

```text
commit test
rollback test
multiple statements
transaction state validation
```

---

# PHASE 11 — Concurrency

Implement basic locking/synchronization.

Protect:

```text
BufferPool
Catalog
TransactionManager
page operations
shared indexes
```

Use appropriate mutexes.

Create multithreaded tests.

### Gate

Run concurrent operations without:

```text
data races
deadlocks
corruption
```

Do not claim full ACID isolation unless actually implemented.

---

# PHASE 12 — WAL

Implement:

```text
LogManager
WAL records
LSN
flush
transaction log records
```

Ensure the WAL rule:

```text
log first
data page later
```

is respected.

### Gate

Tests must verify WAL persistence and ordering.

---

# PHASE 13 — Crash Recovery

Implement:

```text
RecoveryManager
startup recovery
redo
undo where required
unclean shutdown detection
```

### Gate

Simulated crash tests must pass.

The database must restart into a consistent state.

---

# PHASE 14 — CLI Polish

Create a useful interactive shell.

Example:

```text
EmberDB v0.1

EmberDB> .tables

users
orders

EmberDB> .schema users

id      INT
name    VARCHAR
age     INT

EmberDB> SELECT * FROM users;
```

Useful meta commands may include:

```text
.tables
.schema table
.help
.exit
```

SQL itself remains the main interface.

---

# PHASE 15 — HTTP API

Add:

```text
POST /api/query
GET /api/tables
GET /api/schema/:table
GET /api/health
```

The API must call the same engine used by the CLI.

Do not duplicate query execution logic.

Architecture:

```text
CLI ────────┐
            │
Web API ────┼──→ Database Engine
            │
Tests ──────┘
```

### Gate

All API tests must pass.

---

# PHASE 16 — React Web Interface

Build the basic SQL console.

Required:

```text
SQL editor
Execute
Results
Errors
Tables
Schema
Execution time
Row count
Query history
```

Keep UI intentionally simple.

### Gate

From the browser:

```sql
CREATE TABLE
INSERT
SELECT
UPDATE
DELETE
JOIN
GROUP BY
```

must execute through the actual database.

---

# PHASE 17 — Integration + Final Hardening

Run the entire system.

Test:

```text
CLI
HTTP API
React UI
Storage
Parser
Planner
Executor
Indexes
Transactions
WAL
Recovery
JOINs
```

Perform restart tests.

Perform crash tests.

Perform concurrency tests.

Perform end-to-end SQL tests.

---

# PHASE 18 — Documentation + Interview Readiness

Complete:

```text
README
Architecture
Storage format
SQL documentation
Transactions
Recovery
Design decisions
Known limitations
```

Add architecture diagrams.

Document important algorithms:

```text
B+ Tree insertion
Buffer pool eviction
Nested loop join
Query planning
Transaction lifecycle
WAL
Recovery
```

Create a section:

```text
"How EmberDB Works"
```

that can be explained in an interview in approximately 10–15 minutes.

---

# 52. Phase Gate Protocol

This is extremely important.

At the end of EVERY phase:

1. Compile the project.
2. Run unit tests.
3. Run integration tests relevant to the phase.
4. Run existing tests from previous phases.
5. Fix regressions.
6. Update documentation.
7. Record the completed phase in `docs/progress.md`.
8. Only then begin the next phase.

Create:

```text
docs/progress.md
```

with:

```text
Phase 0 — COMPLETE
Phase 1 — COMPLETE
Phase 2 — IN PROGRESS
...
```

For each completed phase record:

```text
Implemented
Tests
Known issues
Design decisions
```

---

# 53. Regression Rule

A later phase must NEVER break an earlier phase.

Before moving from Phase N to Phase N+1:

```text
ALL previous tests must pass.
```

If a later implementation breaks an earlier subsystem:

```text
STOP
FIX REGRESSION
RERUN TESTS
ONLY THEN CONTINUE
```

Do not disable or weaken tests to make the build pass.

---

# 54. No Fake Tests

Never write tests that merely verify:

```text
function returns true
```

without testing actual behavior.

Bad:

```cpp
EXPECT_TRUE(databaseStarted);
```

Good:

```text
insert record
close database
reopen database
read record
compare actual contents
```

Tests should prove behavior.

---

# 55. No Silent Simplification

If an implementation is simplified, document it.

Example:

```text
Full PostgreSQL supports X.

EmberDB currently supports Y.

Reason:
Educational scope.
```

Do not silently pretend to implement functionality that does not exist.

---

# 56. No Premature Features

Do NOT add:

```text
authentication
cloud deployment
Docker
Kubernetes
distributed databases
replication
sharding
stored procedures
triggers
views
materialized views
full-text search
advanced optimizer
```

unless the entire core project is already complete.

The priority is:

```text
Storage
→ SQL
→ Execution
→ JOINs
→ Indexes
→ Transactions
→ WAL
→ Recovery
→ Interface
```

---

# 57. No Premature Frontend Work

Do not spend time making the frontend beautiful.

The frontend exists only to answer:

> "Can I interact with my database easily and see whether it works?"

The CLI and automated tests are more important.

---

# 58. Database File Layout

The database should eventually have a clear on-disk structure.

For example:

```text
data/
└── mydb/
    ├── catalog.db
    ├── data.db
    ├── indexes.db
    └── wal.log
```

The exact format can change.

Document the final design.

Do not create arbitrary files without documenting their purpose.

---

# 59. SQL Session Lifecycle

A database session should conceptually work as:

```text
open database
      ↓
create session
      ↓
execute SQL
      ↓
begin transaction if requested
      ↓
execute statements
      ↓
commit / rollback
      ↓
close session
```

CLI and HTTP requests must eventually use the same database engine abstractions.

---

# 60. Important Architectural Boundary

Maintain strict separation:

```text
SQL layer
    ↓
Query layer
    ↓
Storage layer
```

SQL code must not directly manipulate raw disk pages.

For example:

BAD:

```cpp
SELECT implementation
    ↓
fstream
```

GOOD:

```text
SELECT
 ↓
Planner
 ↓
Executor
 ↓
TableHeap / Index
 ↓
BufferPool
 ↓
DiskManager
```

This separation is one of the most important architectural requirements.

---

# 61. Code Quality Requirements

Prefer:

```text
small classes
clear interfaces
single responsibility
RAII
const correctness
strong types
explicit ownership
```

Avoid:

```text
god classes
global mutable state
huge functions
copy-pasted logic
magic numbers
hard-coded schemas
```

Use comments to explain:

```text
WHY
```

not obvious:

```text
WHAT
```

Bad:

```cpp
// increment i
i++;
```

Good:

```cpp
// Advance the slot pointer because the current slot was deleted.
slotIndex++;
```

---

# 62. Debugging Requirements

When debugging:

1. Reproduce the problem.
2. Write a test that demonstrates it.
3. Fix the implementation.
4. Verify the test passes.
5. Run the full regression suite.

Do not patch symptoms.

---

# 63. Git Discipline

Use meaningful commits.

Examples:

```text
feat(storage): implement disk manager
feat(storage): add slotted pages
feat(sql): implement lexer
feat(sql): implement SELECT parser
feat(execution): add sequential scan
feat(join): implement nested loop join
feat(index): implement B+ tree insertion
feat(tx): implement transactions
feat(recovery): implement WAL
feat(api): add query endpoint
feat(ui): add SQL console
```

Do not make commits such as:

```text
stuff
changes
final
final2
working
test
```

---

# 64. Required End-State Demo

At the end of the project, the following demonstration should work.

Start EmberDB.

Create tables:

```sql
CREATE TABLE users (
    id INT,
    name VARCHAR,
    age INT
);

CREATE TABLE orders (
    id INT,
    user_id INT,
    amount DOUBLE
);
```

Insert:

```sql
INSERT INTO users VALUES (1, 'Faizaan', 23);
INSERT INTO users VALUES (2, 'Ahmed', 24);
INSERT INTO users VALUES (3, 'John', 22);

INSERT INTO orders VALUES (101, 1, 1500);
INSERT INTO orders VALUES (102, 1, 500);
INSERT INTO orders VALUES (103, 2, 2000);
```

Query:

```sql
SELECT *
FROM users;
```

Filter:

```sql
SELECT name, age
FROM users
WHERE age > 22
ORDER BY age DESC;
```

Aggregate:

```sql
SELECT user_id, COUNT(*), SUM(amount)
FROM orders
GROUP BY user_id;
```

JOIN:

```sql
SELECT users.name, orders.amount
FROM users
INNER JOIN orders
ON users.id = orders.user_id;
```

LEFT JOIN:

```sql
SELECT users.name, orders.amount
FROM users
LEFT JOIN orders
ON users.id = orders.user_id;
```

Index:

```sql
CREATE INDEX idx_orders_user_id
ON orders(user_id);
```

Then query:

```sql
SELECT *
FROM orders
WHERE user_id = 1;
```

The planner should be capable of choosing an index scan.

Transaction:

```sql
BEGIN;

UPDATE users
SET age = 99
WHERE id = 1;

ROLLBACK;
```

Verify:

```sql
SELECT *
FROM users
WHERE id = 1;
```

Age must still be `23`.

Commit:

```sql
BEGIN;

UPDATE users
SET age = 24
WHERE id = 1;

COMMIT;
```

Restart EmberDB.

Verify:

```sql
SELECT *
FROM users
WHERE id = 1;
```

Age must remain `24`.

Finally, access the same database through the browser UI.

---

# 65. Definition of Done

EmberDB is considered complete when:

## Storage

- [ ] Pages
- [ ] Disk manager
- [ ] Records
- [ ] Slotted pages
- [ ] Table storage
- [ ] Buffer pool
- [ ] Persistence

## SQL

- [ ] Lexer
- [ ] Parser
- [ ] CREATE TABLE
- [ ] INSERT
- [ ] SELECT
- [ ] UPDATE
- [ ] DELETE
- [ ] WHERE
- [ ] ORDER BY
- [ ] LIMIT
- [ ] Expressions
- [ ] Aggregations
- [ ] GROUP BY

## Relational Processing

- [ ] INNER JOIN
- [ ] LEFT JOIN
- [ ] Multiple-table queries
- [ ] Nested Loop Join

## Indexing

- [ ] B+ Tree
- [ ] Search
- [ ] Insert
- [ ] Split
- [ ] Range scan
- [ ] Index scan
- [ ] Basic planner

## Transactions

- [ ] BEGIN
- [ ] COMMIT
- [ ] ROLLBACK
- [ ] Transaction manager
- [ ] Basic concurrency

## Durability

- [ ] WAL
- [ ] Log flushing
- [ ] Crash recovery
- [ ] Recovery tests

## Interfaces

- [ ] CLI
- [ ] HTTP API
- [ ] React SQL console

## Engineering

- [ ] Unit tests
- [ ] Integration tests
- [ ] End-to-end tests
- [ ] Persistence tests
- [ ] Crash tests
- [ ] Concurrency tests
- [ ] Documentation

---

# 66. Final Instruction to the Coding Agent

You are not being asked to merely generate a large amount of code.

You are being asked to **build a functioning database system incrementally**.

Always prioritize:

```text
Understanding
    ↓
Correct architecture
    ↓
Small implementation
    ↓
Tests
    ↓
Integration
    ↓
Next phase
```

Do not skip foundational work.

Do not jump directly to JOINs, indexes, transactions, or the UI.

Do not declare a phase complete without running its tests.

Do not move to the next phase if the current phase has failing tests.

When a phase is complete:

```text
1. Run tests.
2. Fix failures.
3. Run regression tests.
4. Update docs/progress.md.
5. Document important design decisions.
6. Proceed to the next phase.
```

If something is genuinely blocked by an external dependency or an architectural decision, stop at that phase and clearly explain the blocker rather than silently implementing a fundamentally different architecture.

The final product should be a **small, real relational database written in C++17 from scratch**, with its own storage engine, SQL execution engine, indexing, joins, transactions, WAL/recovery, CLI, HTTP API, and minimal browser interface.

The project should be something the developer can open in an interview and explain from:

```text
SQL
 ↓
Lexer
 ↓
Parser
 ↓
Planner
 ↓
Executor
 ↓
Operators
 ↓
Indexes / Tables
 ↓
Buffer Pool
 ↓
Pages
 ↓
Disk
 ↓
WAL / Recovery
```

Every major arrow in that diagram must correspond to real code in this repository.
