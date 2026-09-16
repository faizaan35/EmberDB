# EmberDB Write-Ahead Logging & Crash Recovery Specification

EmberDB implements Write-Ahead Logging (WAL) and an ARIES-style crash recovery protocol to maintain durability and atomicity across unexpected process crashes, power outages, and system aborts.

---

## 1. The Core WAL Invariant

> **WAL Rule**: A dirty database page must **never** be written to disk until all log records describing modifications up to that page's `LSN` have been flushed to the append-only WAL file.

$$\text{flushed\_lsn} \ge \text{page.GetLSN()} \quad \text{before} \quad \text{DiskManager::WritePage(page)}$$

This invariant is strictly enforced inside `BufferPoolManager::FlushPage(page_id)` and `BufferPoolManager::FlushAllPages()`. When an in-memory page must be evicted or written to disk, the Buffer Pool Manager automatically coordinates with `LogManager`:
```cpp
if (log_mgr_ != nullptr) {
    log_mgr_->FlushLogBufferUpTo(page->GetLSN());
}
disk_mgr_->WritePage(page_id, page->GetData());
```

---

## 2. WAL Log Record Binary Structure

Log records are appended to an in-memory 64KB log buffer and flushed sequentially to disk (`.wal` file). Every log record begins with a fixed 32-byte header:

```text
+-----------------------------------------------------------------------+
|  LOG RECORD HEADER (32 bytes)                                         |
|  - size (4 bytes, uint32_t): Total length of log record in bytes      |
|  - lsn (8 bytes, lsn_t): Monotonically increasing Log Sequence Number |
|  - prev_lsn (8 bytes, lsn_t): Previous LSN for this transaction       |
|  - txn_id (8 bytes, txn_id_t): Transaction identifier                 |
|  - type (4 bytes, LogRecordType): Control or DML operation            |
+-----------------------------------------------------------------------+
|  PAYLOAD (Variable length)                                            |
|  - table_name_length (2B) + table_name string                         |
|  - page_id (4B) + slot_id (4B) (RID)                                  |
|  - before_image: Serialized Record prior to modification (for Undo)   |
|  - after_image: Serialized Record resulting from modification (Redo)  |
+-----------------------------------------------------------------------+
```

### Supported Log Record Types:
* `BEGIN`: Marks the initiation of a transaction.
* `COMMIT`: Marks successful transaction completion (synchronously flushed).
* `ABORT`: Marks intentional rollback or aborted transaction.
* `INSERT`: Records newly inserted tuple payload (after-image) and assigned `RID`.
* `UPDATE`: Records target `RID`, tuple before-image (for undo), and tuple after-image (for redo).
* `DELETE`: Records target `RID` and deleted tuple before-image (for undo).
* `CHECKPOINT_END`: Appended during graceful database shutdown to indicate a clean state.

---

## 3. Crash Recovery Algorithm (ARIES Protocol)

When `EmberDBInstance::Open()` opens a database directory, it invokes `RecoveryManager::NeedsRecovery()`.

### 3.1 Unclean Shutdown Detection
The recovery manager inspects the WAL file:
* If the WAL does not exist or has size 0, the database is clean.
* If the final record in the WAL is `CHECKPOINT_END` and no active transactions remain, `NeedsRecovery()` returns `false`.
* If the last record is not a clean checkpoint or active uncommitted transactions exist, an unclean shutdown is detected and `RecoveryManager::Recover()` executes.

### 3.2 Phase 1: Analysis Pass
* The log file is scanned forward from start to finish.
* Identifies:
  - Monotonic `next_lsn` to restore log sequence continuity without reset.
  - Active transactions at crash time (`active_txns_`).
  - Committed transactions (`committed_txns_`).
* Any transaction that has a `BEGIN` record but neither a `COMMIT` nor an `ABORT` record is classified as an uncommitted **Loser Transaction**.

### 3.3 Phase 2: Redo Pass ("Repeating History")
* Scans forward through all DML records in ascending LSN order.
* Replays all mutations from committed transactions that had not yet reached disk before the crash:
  - **Redo Insert**: Reconstructs the exact binary tuple and re-inserts it into the target `TableHeap` at the original `RID` using `SlottedPage::RedoInsert()`. Also inserts the key into secondary B+ Tree indexes.
  - **Redo Update**: Writes the `after_image` into the target slot and updates secondary indexes.
  - **Redo Delete**: Marks the target slot tombstoned and removes keys from secondary indexes.
* This restores the physical state of all pages to the exact instant of the crash.

### 3.4 Phase 3: Undo Pass ("Rolling Back Losers")
* Traverses the log backward in reverse LSN order.
* For every operation originating from an uncommitted loser transaction:
  - **Undo Insert**: Deletes the inserted record and removes its key from secondary B+ tree indexes.
  - **Undo Update**: Overwrites the tuple with its `before_image` and restores previous index keys.
  - **Undo Delete**: Clears the tombstone flag in the slotted page (`SlottedPage::RollbackDelete`), reviving the row and restoring index entries.
* Writes an `ABORT` log record for each reversed loser transaction.

### 3.5 Post-Recovery Stabilization
* Flushes all recovered buffer pool pages to disk.
* Appends a `CHECKPOINT_END` record to the WAL and flushes the log.
* The database engine transitions cleanly to normal client operations.
