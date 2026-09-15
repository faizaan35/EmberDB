# ForgeDB On-Disk Storage Format

ForgeDB organizes all database files using fixed-size blocks called **Pages**.

## 1. Page Specifications

* **Page Size**: `4096 bytes` (4 KB), matching the default OS memory page and disk cluster size.
* **Addressing**: Addressed by a zero-indexed 32-bit integer `page_id_t` (0, 1, 2, ...).
* **Physical Offset**: Page offset in file = `page_id * 4096`.

## 2. Slotted Page Layout

To accommodate variable-length records (e.g. `VARCHAR`), pages use a slotted-page architecture.

```
+-----------------------------------------------------------------------+
|  PAGE HEADER (24 bytes)                                               |
|  - page_id (4B)                                                       |
|  - lsn (8B)                                                           |
|  - prev_page_id (4B)                                                  |
|  - next_page_id (4B)                                                  |
|  - slot_count (2B)                                                    |
|  - free_space_pointer (2B)                                            |
+-----------------------------------------------------------------------+
|  SLOT DIRECTORY (grows downwards)                                     |
|  - Slot 0: offset (2B), length (2B)                                   |
|  - Slot 1: offset (2B), length (2B)                                   |
|  ...                                                                  |
|  - Slot N: offset (2B), length (2B)                                   |
+-----------------------------------------------------------------------+
|                       FREE SPACE                                      |
+-----------------------------------------------------------------------+
|  RECORD DATA (grows upwards from end of page)                         |
|  ...                                                                  |
|  Record 1                                                             |
|  Record 0                                                             |
+-----------------------------------------------------------------------+
```

### Invariants:
1. The **Slot Directory** starts immediately following the 24-byte header and expands forward towards higher memory addresses.
2. **Record Data** is placed at the end of the page and expands backwards toward lower memory addresses.
3. Free space is contiguous between the end of the slot directory and the `free_space_pointer`.
4. An entry in the slot directory can be marked deleted (`offset = 0`, `length = 0` or a tombstone flag) without disturbing the slot indexes of other records.

## 3. Record Identifier (RID)

Every record stored in ForgeDB is addressed by a stable 64-bit identifier composed of:
* `page_id` (32-bit): The physical page where the record resides.
* `slot_id` (32-bit): The index within the page's slot directory.

This allows records to be compacted or relocated within the page during deletion/update without invalidating external references or index pointers.

## 4. Record Binary Layout

A serialized tuple contains:
1. **Null Bitmap**: Bitmask indicating `NULL` status for each column.
2. **Fixed-Width Fields**: Standard integer, bigint, double, or boolean values packed in column sequence.
3. **Variable-Width Fields**: 2-byte length header followed by raw character bytes.
