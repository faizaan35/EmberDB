# EmberDB Transactions Specification

EmberDB guarantees ACID properties within its supported educational scope.

## 1. Transaction Model

Transactions are delimited by explicit SQL statements:
* `BEGIN`: Initiates a new active transaction and assigns a monotonically increasing `txn_id_t`.
* `COMMIT`: Persists all modifications made by the transaction, writes a COMMIT log record to WAL, and flushes the log.
* `ROLLBACK`: Discards or reverses uncommitted modifications using before-images recorded in WAL / undo logs, and releases held locks.

## 2. Transaction States

```
        BEGIN
          │
          ▼
      [ ACTIVE ]
       │      │
 COMMIT│      │ROLLBACK / ERROR
       ▼      ▼
[COMMITTED] [ABORTED]
```

* `ACTIVE`: Modifications are occurring under this transaction.
* `COMMITTED`: The commit log record has been flushed to durable storage.
* `ABORTED`: The transaction encountered an error or explicit rollback; changes are undone.

## 3. Concurrency Control

* Shared latching for read access to catalog and buffer pool frames.
* Exclusive latching for modifications.
* Strict synchronization across multi-threaded operations to guarantee memory safety.
