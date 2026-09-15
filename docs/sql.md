# EmberDB SQL Specification

EmberDB supports an intentional, clean subset of standard ANSI SQL tailored for relational systems engineering.

## 1. Supported Data Types

* `INT`: 32-bit signed integer.
* `BIGINT`: 64-bit signed integer.
* `DOUBLE`: 64-bit IEEE double-precision floating point.
* `BOOLEAN`: 1-byte boolean (`TRUE` / `FALSE`).
* `VARCHAR`: Variable-length UTF-8 encoded string.

## 2. Data Definition Language (DDL)

### Create Table
```sql
CREATE TABLE users (
    id INT,
    name VARCHAR,
    age INT
);
```

### Drop Table
```sql
DROP TABLE users;
```

### Create Index
```sql
CREATE INDEX idx_users_id ON users(id);
```

## 3. Data Manipulation Language (DML)

### Insert
```sql
INSERT INTO users VALUES (1, 'Faizaan', 23);
INSERT INTO users (id, name, age) VALUES (2, 'Ahmed', 24);
```

### Select
```sql
-- All columns
SELECT * FROM users;

-- Specific projections
SELECT id, name FROM users;

-- Filtering with comparisons
SELECT name FROM users WHERE age > 20;

-- Ordering and limits
SELECT * FROM users ORDER BY age DESC LIMIT 10;
```

### Update
```sql
UPDATE users SET age = 24 WHERE id = 1;
UPDATE users SET age = 25, name = 'Updated' WHERE id = 1;
```

### Delete
```sql
DELETE FROM users WHERE id = 1;
DELETE FROM users;
```

## 4. Joins

```sql
-- Inner Join
SELECT users.name, orders.amount
FROM users
INNER JOIN orders
ON users.id = orders.user_id;

-- Left Outer Join
SELECT users.name, orders.amount
FROM users
LEFT JOIN orders
ON users.id = orders.user_id;
```

## 5. Aggregations & Grouping

```sql
SELECT COUNT(*) FROM users;
SELECT AVG(age), MIN(age), MAX(age), SUM(age) FROM users;

SELECT age, COUNT(*)
FROM users
GROUP BY age
ORDER BY age DESC;
```

## 6. Transactions

```sql
BEGIN;
UPDATE accounts SET balance = balance - 100 WHERE id = 1;
UPDATE accounts SET balance = balance + 100 WHERE id = 2;
COMMIT;
```

```sql
BEGIN;
DELETE FROM users WHERE id = 10;
ROLLBACK;
```
