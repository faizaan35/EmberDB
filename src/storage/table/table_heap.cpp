#include "emberdb/storage/table/table_heap.h"
#include <vector>

namespace emberdb {

TableHeap::TableHeap(BufferPoolManager* bpm, page_id_t first_page_id)
    : bpm_(bpm), disk_mgr_(bpm->GetDiskManager()), first_page_id_(first_page_id), last_page_id_(first_page_id) {
    if (first_page_id_ != INVALID_PAGE_ID) {
        page_id_t curr = first_page_id_;
        while (curr != INVALID_PAGE_ID) {
            last_page_id_ = curr;
            Page* p = bpm_->FetchPage(curr);
            if (!p) break;
            SlottedPage page(p->GetData());
            page_id_t next = page.GetNextPageId();
            bpm_->UnpinPage(curr, false);
            curr = next;
        }
    }
}

TableHeap::TableHeap(DiskManager* disk_mgr, page_id_t first_page_id)
    : owned_bpm_(std::make_unique<BufferPoolManager>(64, disk_mgr)),
      bpm_(owned_bpm_.get()),
      disk_mgr_(disk_mgr),
      first_page_id_(first_page_id),
      last_page_id_(first_page_id) {
    if (first_page_id_ != INVALID_PAGE_ID) {
        page_id_t curr = first_page_id_;
        while (curr != INVALID_PAGE_ID) {
            last_page_id_ = curr;
            Page* p = bpm_->FetchPage(curr);
            if (!p) break;
            SlottedPage page(p->GetData());
            page_id_t next = page.GetNextPageId();
            bpm_->UnpinPage(curr, false);
            curr = next;
        }
    }
}

Status TableHeap::InsertRecord(Record& record) {
    std::lock_guard<std::mutex> lock(latch_);
    RID rid;

    // Case 1: First page does not exist yet
    if (first_page_id_ == INVALID_PAGE_ID) {
        Page* page = bpm_->NewPage(&first_page_id_);
        if (!page) {
            return Status::IOError("Failed to allocate new page from buffer pool");
        }

        last_page_id_ = first_page_id_;
        SlottedPage sp(page->GetData());
        sp.Init(first_page_id_);

        if (!sp.InsertRecord(record, rid)) {
            bpm_->UnpinPage(first_page_id_, false);
            return Status::InvalidArgument("Record exceeds maximum page capacity");
        }

        record.SetRID(rid);
        bpm_->UnpinPage(first_page_id_, true);

        if (on_first_page_allocated_) {
            on_first_page_allocated_(first_page_id_);
        }
        return Status::OK();
    }

    // Case 2: Try inserting into the current last page
    Page* last_page = bpm_->FetchPage(last_page_id_);
    if (!last_page) {
        return Status::IOError("Failed to fetch last page from buffer pool");
    }

    SlottedPage sp_last(last_page->GetData());
    if (sp_last.InsertRecord(record, rid)) {
        record.SetRID(rid);
        bpm_->UnpinPage(last_page_id_, true);
        return Status::OK();
    }
    bpm_->UnpinPage(last_page_id_, false);

    // Case 3: Last page is full, allocate a new page and chain it
    page_id_t new_page_id = INVALID_PAGE_ID;
    Page* new_page = bpm_->NewPage(&new_page_id);
    if (!new_page) {
        return Status::IOError("Failed to allocate new page from buffer pool");
    }

    SlottedPage sp_new(new_page->GetData());
    sp_new.Init(new_page_id, last_page_id_, INVALID_PAGE_ID);

    if (!sp_new.InsertRecord(record, rid)) {
        bpm_->UnpinPage(new_page_id, false);
        return Status::InvalidArgument("Record exceeds maximum page capacity");
    }

    record.SetRID(rid);

    // Update old last page next pointer
    Page* old_last = bpm_->FetchPage(last_page_id_);
    if (old_last) {
        SlottedPage sp_old(old_last->GetData());
        sp_old.SetNextPageId(new_page_id);
        bpm_->UnpinPage(last_page_id_, true);
    }

    last_page_id_ = new_page_id;
    bpm_->UnpinPage(new_page_id, true);
    return Status::OK();
}

Status TableHeap::GetRecord(const RID& rid, Record& record) {
    std::lock_guard<std::mutex> lock(latch_);
    if (!rid.IsValid()) {
        return Status::InvalidArgument("Invalid RID");
    }

    Page* page = bpm_->FetchPage(rid.page_id);
    if (!page) {
        return Status::NotFound("Page not found in buffer pool: " + std::to_string(rid.page_id));
    }

    SlottedPage sp(page->GetData());
    bool ok = sp.GetRecord(rid, record);
    bpm_->UnpinPage(rid.page_id, false);

    if (!ok) {
        return Status::NotFound("Record not found at " + rid.ToString());
    }

    return Status::OK();
}

Status TableHeap::UpdateRecord(const RID& rid, const Record& new_record) {
    std::lock_guard<std::mutex> lock(latch_);
    if (!rid.IsValid()) {
        return Status::InvalidArgument("Invalid RID");
    }

    Page* page = bpm_->FetchPage(rid.page_id);
    if (!page) {
        return Status::NotFound("Page not found in buffer pool: " + std::to_string(rid.page_id));
    }

    SlottedPage sp(page->GetData());
    bool ok = sp.UpdateRecord(rid, new_record);
    bpm_->UnpinPage(rid.page_id, ok);

    if (!ok) {
        return Status::InvalidArgument("Cannot update record in-place: insufficient space or invalid slot");
    }

    return Status::OK();
}

Status TableHeap::DeleteRecord(const RID& rid) {
    std::lock_guard<std::mutex> lock(latch_);
    if (!rid.IsValid()) {
        return Status::InvalidArgument("Invalid RID");
    }

    Page* page = bpm_->FetchPage(rid.page_id);
    if (!page) {
        return Status::NotFound("Page not found in buffer pool: " + std::to_string(rid.page_id));
    }

    SlottedPage sp(page->GetData());
    bool ok = sp.DeleteRecord(rid);
    bpm_->UnpinPage(rid.page_id, ok);

    if (!ok) {
        return Status::NotFound("Record not found for deletion at " + rid.ToString());
    }

    return Status::OK();
}

Status TableHeap::RollbackDelete(const RID& rid, const Record& old_record) {
    std::lock_guard<std::mutex> lock(latch_);
    if (!rid.IsValid()) {
        return Status::InvalidArgument("Invalid RID");
    }

    Page* page = bpm_->FetchPage(rid.page_id);
    if (!page) {
        return Status::NotFound("Page not found in buffer pool: " + std::to_string(rid.page_id));
    }

    SlottedPage sp(page->GetData());
    bool ok = sp.RollbackDelete(rid, old_record);
    bpm_->UnpinPage(rid.page_id, ok);

    if (!ok) {
        return Status::InvalidArgument("Failed to rollback deleted record at " + rid.ToString());
    }

    return Status::OK();
}

TableIterator TableHeap::Begin() {
    if (first_page_id_ == INVALID_PAGE_ID) {
        return End();
    }
    return TableIterator(this, RID(first_page_id_, 0));
}

TableIterator TableHeap::End() {
    return TableIterator(this, RID(INVALID_PAGE_ID, INVALID_SLOT_ID));
}

// ---------------------------------------------------------------------------
// TableIterator Implementation
// ---------------------------------------------------------------------------

TableIterator::TableIterator(TableHeap* table_heap, RID rid)
    : table_heap_(table_heap), current_rid_(rid) {
    if (current_rid_.IsValid()) {
        Advance();
    }
}

void TableIterator::Advance() {
    if (current_rid_.page_id == INVALID_PAGE_ID) {
        return;
    }

    BufferPoolManager* bpm = table_heap_->GetBufferPoolManager();
    page_id_t curr_page_id = current_rid_.page_id;
    slot_id_t curr_slot_id = current_rid_.slot_id;

    while (curr_page_id != INVALID_PAGE_ID) {
        Page* page = bpm->FetchPage(curr_page_id);
        if (!page) {
            current_rid_ = RID(INVALID_PAGE_ID, INVALID_SLOT_ID);
            return;
        }

        SlottedPage sp(page->GetData());
        uint16_t slot_count = sp.GetSlotCount();

        while (curr_slot_id < slot_count) {
            Slot s = sp.GetSlot(curr_slot_id);
            if (s.size > 0) {
                // Found a valid record
                current_rid_ = RID(curr_page_id, curr_slot_id);
                current_record_ = Record(page->GetData() + s.offset, s.size, current_rid_);
                bpm->UnpinPage(curr_page_id, false);
                return;
            }
            ++curr_slot_id;
        }

        page_id_t next_page_id = sp.GetNextPageId();
        bpm->UnpinPage(curr_page_id, false);

        curr_page_id = next_page_id;
        curr_slot_id = 0;
    }

    current_rid_ = RID(INVALID_PAGE_ID, INVALID_SLOT_ID);
}

TableIterator& TableIterator::operator++() {
    if (current_rid_.IsValid()) {
        ++current_rid_.slot_id;
        Advance();
    }
    return *this;
}

bool TableIterator::operator==(const TableIterator& o) const {
    return current_rid_ == o.current_rid_;
}

} // namespace emberdb
