#pragma once

#include "emberdb/storage/disk/disk_manager.h"
#include "emberdb/storage/buffer/buffer_pool_manager.h"
#include "emberdb/storage/page/slotted_page.h"
#include "emberdb/storage/record/record.h"
#include <memory>
#include <functional>

namespace emberdb {

class TableIterator;

/**
 * TableHeap represents a physical relation on disk, consisting of a doubly-linked chain of SlottedPages.
 */
class TableHeap {
public:
    explicit TableHeap(BufferPoolManager* bpm, page_id_t first_page_id = INVALID_PAGE_ID);
    explicit TableHeap(DiskManager* disk_mgr, page_id_t first_page_id = INVALID_PAGE_ID);

    Status InsertRecord(Record& record);
    Status GetRecord(const RID& rid, Record& record);
    Status UpdateRecord(const RID& rid, const Record& new_record);
    Status DeleteRecord(const RID& rid);

    page_id_t GetFirstPageId() const { return first_page_id_; }
    DiskManager* GetDiskManager() const { return disk_mgr_; }
    BufferPoolManager* GetBufferPoolManager() const { return bpm_; }

    void SetOnFirstPageAllocated(std::function<void(page_id_t)> cb) {
        on_first_page_allocated_ = std::move(cb);
    }

    TableIterator Begin();
    TableIterator End();

private:
    std::unique_ptr<BufferPoolManager> owned_bpm_;
    BufferPoolManager* bpm_;
    DiskManager* disk_mgr_;
    page_id_t first_page_id_{INVALID_PAGE_ID};
    page_id_t last_page_id_{INVALID_PAGE_ID};
    std::function<void(page_id_t)> on_first_page_allocated_;
};

/**
 * TableIterator performs a sequential scan over all valid records in a TableHeap.
 */
class TableIterator {
public:
    TableIterator(TableHeap* table_heap, RID rid);

    const Record& operator*() const { return current_record_; }
    const Record* operator->() const { return &current_record_; }

    TableIterator& operator++();
    bool operator==(const TableIterator& o) const;
    bool operator!=(const TableIterator& o) const { return !(*this == o); }

    RID GetRID() const { return current_rid_; }

private:
    void Advance();

    TableHeap* table_heap_;
    RID current_rid_{INVALID_PAGE_ID, INVALID_SLOT_ID};
    Record current_record_;
};

} // namespace emberdb
