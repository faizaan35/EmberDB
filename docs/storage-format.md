# EmberDB On-Disk Storage Format

EmberDB organizes all database files using fixed-size blocks called **Pages**.

---

## 1. Page Specifications

* **Page Size**: `4096 bytes` (4 KB), precisely matching the default operating system page size and disk cluster size.
* **Addressing**: Addressed by a zero-indexed 32-bit integer `page_id_t` (0, 1, 2, ...).
* **Physical File Offset**: Physical disk offset = `page_id * 4096`.
* **Special Pages**:
  - `page_id = 0`: Dedicated Catalog Page (`CATALOG_PAGE_ID`), storing system metadata, schemas, and table definitions.
  - `page_id >= 1`: Data pages, slotted record pages, and B+ Tree node pages.

---

## 2. Slotted Page Layout

To efficiently store variable-length records (e.g. `VARCHAR`) without internal fragmentation, data pages use a slotted-page architecture.

```text
+-----------------------------------------------------------------------+
|  PAGE HEADER (24 bytes)                                               |
|  - page_id (4 bytes, uint32_t)                                        |
|  - lsn (8 bytes, lsn_t / uint64_t)                                    |
|  - prev_page_id (4 bytes, int32_t)                                    |
|  - next_page_id (4 bytes, int32_t)                                    |
|  - slot_count (2 bytes, uint16_t)                                     |
|  - free_space_pointer (2 bytes, uint16_t)                             |
+-----------------------------------------------------------------------+
|  SLOT DIRECTORY (grows downwards towards page center)                 |
|  - Slot 0: offset (2B, uint16_t), length (2B, uint16_t)               |
|  - Slot 1: offset (2B, uint16_t), length (2B, uint16_t)               |
|  ...                                                                  |
|  - Slot N: offset (2B, uint16_t), length (2B, uint16_t)               |
+-----------------------------------------------------------------------+
|                       CONTIGUOUS FREE SPACE                           |
|       (from end of slot directory to free_space_pointer)              |
+-----------------------------------------------------------------------+
|  RECORD DATA (grows upwards from end of page towards page center)     |
|  ...                                                                  |
|  Record 1 (raw binary tuple payload)                                  |
|  Record 0 (raw binary tuple payload)                                  |
+-----------------------------------------------------------------------+
```

### 2.1 Page Header Fields
| Field | Byte Offset | Size | Type | Description |
|---|---|---|---|---|
| `page_id` | 0 | 4 | `uint32_t` | Unique identifier of this page |
| `lsn` | 4 | 8 | `uint64_t` | Log Sequence Number of the last WAL record that modified this page |
| `prev_page_id` | 12 | 4 | `int32_t` | Previous page pointer in double-linked table heap (`-1` if head) |
| `next_page_id` | 16 | 4 | `int32_t` | Next page pointer in double-linked table heap (`-1` if tail) |
| `slot_count` | 20 | 2 | `uint16_t` | Number of slot entries in the directory |
| `free_space_pointer` | 22 | 2 | `uint16_t` | Byte offset where the next inserted record payload will end |

### 2.2 Slot Directory Mechanics
* Each slot is 4 bytes: `offset` (uint16_t) and `length` (uint16_t).
* Slot 0 begins at byte offset 24 (immediately after the header).
* Available contiguous free space is calculated as:
  $$\text{FreeSpace} = \text{free\_space\_pointer} - (24 + \text{slot\_count} \times 4)$$
* Record payloads are stored at `[offset, offset + length)`.
* **Tombstones / Deletions**: When a record is deleted, its slot entry is tombstoned by setting `offset = 0` and `length = 0`. The slot index is preserved so external Record Identifiers (`RID`) remain stable.

---

## 3. Record Identifier (RID)

Every stored record is identified by a lightweight 8-byte structure:
* `page_id` (32-bit `page_id_t`): Physical disk page identifier.
* `slot_id` (32-bit `slot_id_t`): Index in the page's slot directory.

Because the `RID` references a slot index rather than a physical byte offset, records can be updated in-place or compacted within the page without invalidating secondary B+ tree index entries or foreign references.

---

## 4. Binary Tuple Serialization Format

Tuples are serialized into a compact, self-contained binary layout:

```text
+-------------------+-----------------------------+-----------------------------+
| Null Bitmap (N B) | Fixed-Width Columns         | Variable-Width Columns      |
| (1 bit per col)   | INT(4B), BIGINT(8B), etc.   | 2B length + raw string bytes|
+-------------------+-----------------------------+-----------------------------+
```

1. **Null Bitmap**:
   - Size: `ceil(column_count / 8)` bytes.
   - Bit $i = 1$ indicates that column $i$ is `NULL`; bit $i = 0$ indicates non-null value.
2. **Fixed-Width Fields**:
   - `BOOLEAN`: 1 byte (`uint8_t`).
   - `INTEGER`: 4 bytes (`int32_t`, little-endian).
   - `BIGINT`: 8 bytes (`int64_t`, little-endian).
   - `DOUBLE`: 8 bytes (IEEE-754 double precision).
3. **Variable-Width Fields** (`VARCHAR`):
   - 2-byte unsigned integer length prefix followed immediately by the UTF-8 string bytes.

---

## 5. B+ Tree Page Format

B+ Tree index pages also occupy 4096 bytes and share a common 24-byte B+ Tree header:

```text
+-----------------------------------------------------------------------+
|  B+ TREE PAGE HEADER (24 bytes)                                       |
|  - page_type (2B): LEAF (1) or INTERNAL (2)                           |
|  - size (2B): Current number of keys in node                          |
|  - max_size (2B): Maximum capacity before node split                  |
|  - parent_page_id (4B): Parent internal page ID (-1 for root)         |
|  - page_id (4B): Self page ID                                         |
|  - next_page_id (4B): Sibling leaf page ID (Leaf nodes only)           |
|  - lsn (8B): WAL log sequence number                                  |
+-----------------------------------------------------------------------+
```

* **Leaf Nodes**:
  - Store array of `(Key, RID)` pairs.
  - Sibling pointer `next_page_id` connects adjacent leaf pages into a singly-linked list, allowing $O(k)$ range scans without re-traversing the tree.
* **Internal Nodes**:
  - Store array of `(Key, PageId)` pairs plus an initial child pointer `value[0]`.
  - Used strictly for routing point lookups to leaf nodes.

---

## 6. Write-Ahead Log (WAL) Record Format

WAL log files (`.wal`) consist of a contiguous append-only stream of binary log records. Every log record begins with a fixed 32-byte header:

```text
+-----------------------------------------------------------------------+
|  LOG RECORD HEADER (32 bytes)                                         |
|  - size (4B, uint32_t): Total length of this log record in bytes      |
|  - lsn (8B, lsn_t): Monotonically increasing Log Sequence Number      |
|  - prev_lsn (8B, lsn_t): Previous LSN for the same transaction        |
|  - txn_id (8B, txn_id_t): Transaction identifier                     |
|  - type (4B, LogRecordType): BEGIN, COMMIT, ABORT, INSERT, etc.       |
+-----------------------------------------------------------------------+
|  PAYLOAD (variable size depending on type)                            |
|  - table_name_length (2B) + table_name string                         |
|  - page_id (4B) + slot_id (4B)                                        |
|  - before_image (for UPDATE / DELETE undo)                            |
|  - after_image (for INSERT / UPDATE redo)                             |
+-----------------------------------------------------------------------+
```
