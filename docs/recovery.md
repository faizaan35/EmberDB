# ForgeDB Write-Ahead Logging & Crash Recovery

ForgeDB implements Write-Ahead Logging (WAL) and ARIES-style recovery protocols to maintain durability and atomicity in the event of unexpected process crashes or system shutdowns.

## 1. The Core WAL Invariant

> **WAL Protocol**: A dirty data page must never be written to disk until the log record describing that update has been flushed to the WAL log file.

This ensures that if the process terminates immediately after writing a data page, the WAL contains the necessary redo or undo information to reconstruct a consistent state.

## 2. WAL Record Structure

Each log entry is serialized with:
* `LSN` (64-bit Log Sequence Number): Monotonically increasing offset/identifier.
* `prev_lsn` (64-bit): Links log records of the same transaction backwards.
* `txn_id` (64-bit): Transaction identifier.
* `type`: Record type (`BEGIN`, `COMMIT`, `ABORT`, `INSERT`, `UPDATE`, `DELETE`, `CHECKPOINT`).
* `page_id`: Affected data page.
* `slot_id` / `offset`: Location within the page.
* `before_image` / `after_image`: Byte payloads for undo/redo.

## 3. Recovery Protocol

During database startup, the `RecoveryManager` inspects the state:
1. **Analysis Phase**: Scan the log to identify active transactions and dirty pages at the time of shutdown.
2. **Redo Phase**: Replay all logged actions from the earliest dirty page forward to bring the database up to the point of crash.
3. **Undo Phase**: Roll back operations of any transactions that were active (uncommitted) at the time of crash.
