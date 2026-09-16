# EmberDB Transactions & Concurrency Specification

EmberDB implements an ACID transaction model and thread-safe concurrency control tailored for an educational, from-scratch relational database engine.

---

## 1. ACID Guarantees

* **Atomicity**: All DML statements executed between `BEGIN` and `COMMIT` succeed together or fail together. If an error occurs or `ROLLBACK` is issued, all table mutations are reversed in reverse topological order, restoring original tuple states and cleaning up secondary index keys.
* **Consistency**: Every transaction operates on well-formed schemas. Column types, nullability constraints, and catalog metadata are validated at statement parse and bind time.
* **Isolation**: Concurrency control uses reader-writer latches across all shared structures (`Page`, `BufferPoolManager`, `Catalog`, `TableHeap`, `BPlusTreeIndex`), preventing data corruption, torn reads, and memory races.
* **Durability**: Upon `COMMIT`, a `COMMIT` record is written to the WAL and synchronously flushed to disk (`LogManager::FlushLogBuffer()`), guaranteeing survival across sudden crashes.

---

## 2. Transaction State Machine

```text
               BEGIN
                 │
                 ▼
          ┌─────────────┐
          │   ACTIVE    │ ◄─── (DML operations mutate tables & log undo entries)
          └──────┬──────┘
                 │
        ┌────────┴────────┐
        │                 │
     COMMIT            ROLLBACK / ERROR / CRASH
        │                 │
        ▼                 ▼
 ┌─────────────┐   ┌─────────────┐
 │  COMMITTED  │   │   ABORTED   │
 └─────────────┘   └─────────────┘
```

1. **ACTIVE**: A transaction is created via `TransactionManager::Begin()`. It receives a unique, monotonically increasing 64-bit `txn_id_t`. Mutations log undo records into the transaction's private write set.
2. **COMMITTED**: The transaction concludes successfully via `TransactionManager::Commit()`. All table write records are marked permanent, a `COMMIT` log record is appended to the WAL, and the WAL buffer is flushed to disk.
3. **ABORTED**: Triggered by an explicit `ROLLBACK` command or unhandled execution failure. The rollback undo engine iterates backward over the write set and reverses every mutation.

---

## 3. Undo Logging & Rollback Execution Mechanics

While active, each modifying operation records a `TableWriteRecord` inside the `Transaction`:

```cpp
struct TableWriteRecord {
    WType type;             // INSERT, UPDATE, DELETE
    std::string table_name; // Target relation
    RID rid;                // Physical tuple location
    Record before_image;    // Tuple state prior to modification
    Record after_image;     // Newly written tuple state
};
```

### Rollback Process:
When `TransactionManager::Abort(Transaction* txn)` is invoked, it traverses `txn->GetWriteSet()` in **reverse order**:
1. **Reversing INSERT**:
   - The inserted row is deleted from the `TableHeap` via `table->GetTableHeap()->DeleteRecord(rid)`.
   - Secondary B+ tree index entries for newly inserted keys are removed via `BPlusTreeIndex::Remove()`.
2. **Reversing UPDATE**:
   - The tuple is overwritten with its `before_image` via `table->GetTableHeap()->UpdateRecord(before_image, rid)`.
   - Secondary B+ tree indexes restore the old key and remove the updated key.
3. **Reversing DELETE**:
   - The slotted page tombstone is cleared via `SlottedPage::RollbackDelete(slot_id)`, resurrecting the original tuple.
   - Secondary B+ tree indexes re-insert the deleted key pointing to `rid`.

---

## 4. Concurrency Control Primitives

EmberDB implements multi-level synchronization primitives to allow concurrent queries and workers without race conditions:

### 4.1 Page Reader-Writer Latching
Each buffer pool `Page` contains a `mutable std::shared_mutex rwlock_`:
* `RLatch()` / `RUnlatch()`: Shared read lock used during sequential scans and index searches.
* `WLatch()` / `WUnlatch()`: Exclusive write lock used during tuple insertions, updates, and slot allocations.

### 4.2 Catalog Reader-Writer Synchronization
The `Catalog` protects its internal metadata maps with `mutable std::shared_mutex catalog_latch_`:
* Concurrent query planning and schema lookups acquire shared locks (`std::shared_lock`).
* DDL operations (`CreateTable`, `DropTable`, `CreateIndex`) acquire exclusive locks (`std::unique_lock`).

### 4.3 TableHeap & Buffer Pool Synchronization
* `TableHeap` insertion and update routines synchronize page allocation and record positioning using mutexes.
* `BufferPoolManager` synchronizes frame table lookup, free list retrieval, and LRU replacer updates via thread-safe critical sections.
