#include "forgedb/storage/table/table_heap.h"
#include <vector>

namespace forgedb {

TableHeap::TableHeap(DiskManager* disk_mgr, page_id_t first_page_id)
    : disk_mgr_(disk_mgr), first_page_id_(first_page_id), last_page_id_(first_page_id) {
    if (first_page_id_ != INVALID_PAGE_ID) {
        // Find the actual last page in the chain
        page_id_t curr = first_page_id_;
        std::vector<char> buf(PAGE_SIZE);
        while (curr != INVALID_PAGE_ID) {
            last_page_id_ = curr;
            auto status = disk_mgr_->ReadPage(curr, buf.data());
            if (!status.ok()) break;
            SlottedPage page(buf.data());
            curr = page.GetNextPageId();
        }
    }
}

Status TableHeap::InsertRecord(Record& record) {
    if (!disk_mgr_->IsOpen()) {
        return Status::IOError("Disk manager is not open");
    }

    std::vector<char> page_buf(PAGE_SIZE);
    RID rid;

    // Case 1: First page does not exist yet
    if (first_page_id_ == INVALID_PAGE_ID) {
        auto alloc_res = disk_mgr_->AllocatePage();
        if (!alloc_res.ok()) {
            return alloc_res.status();
        }

        first_page_id_ = *alloc_res;
        last_page_id_ = first_page_id_;

        SlottedPage page(page_buf.data());
        page.Init(first_page_id_);

        if (!page.InsertRecord(record, rid)) {
            return Status::InvalidArgument("Record exceeds maximum page capacity");
        }

        record.SetRID(rid);
        auto write_status = disk_mgr_->WritePage(first_page_id_, page_buf.data());
        if (!write_status.ok()) return write_status;

        if (on_first_page_allocated_) {
            on_first_page_allocated_(first_page_id_);
        }
        return Status::OK();
    }

    // Case 2: Try inserting into the current last page
    auto read_status = disk_mgr_->ReadPage(last_page_id_, page_buf.data());
    if (!read_status.ok()) {
        return read_status;
    }

    SlottedPage last_page(page_buf.data());
    if (last_page.InsertRecord(record, rid)) {
        record.SetRID(rid);
        return disk_mgr_->WritePage(last_page_id_, page_buf.data());
    }

    // Case 3: Last page is full, allocate a new page and chain it
    auto alloc_res = disk_mgr_->AllocatePage();
    if (!alloc_res.ok()) {
        return alloc_res.status();
    }

    page_id_t new_page_id = *alloc_res;

    // Update last_page's next_page pointer
    last_page.SetNextPageId(new_page_id);
    auto write_old_status = disk_mgr_->WritePage(last_page_id_, page_buf.data());
    if (!write_old_status.ok()) {
        return write_old_status;
    }

    // Initialize new page
    std::vector<char> new_page_buf(PAGE_SIZE);
    SlottedPage new_page(new_page_buf.data());
    new_page.Init(new_page_id, last_page_id_, INVALID_PAGE_ID);

    if (!new_page.InsertRecord(record, rid)) {
        return Status::InvalidArgument("Record exceeds maximum page capacity");
    }

    record.SetRID(rid);
    last_page_id_ = new_page_id;
    return disk_mgr_->WritePage(new_page_id, new_page_buf.data());
}

Status TableHeap::GetRecord(const RID& rid, Record& record) {
    if (!rid.IsValid()) {
        return Status::InvalidArgument("Invalid RID");
    }

    std::vector<char> page_buf(PAGE_SIZE);
    auto read_status = disk_mgr_->ReadPage(rid.page_id, page_buf.data());
    if (!read_status.ok()) {
        return read_status;
    }

    SlottedPage page(page_buf.data());
    if (!page.GetRecord(rid, record)) {
        return Status::NotFound("Record not found at " + rid.ToString());
    }

    return Status::OK();
}

Status TableHeap::UpdateRecord(const RID& rid, const Record& new_record) {
    if (!rid.IsValid()) {
        return Status::InvalidArgument("Invalid RID");
    }

    std::vector<char> page_buf(PAGE_SIZE);
    auto read_status = disk_mgr_->ReadPage(rid.page_id, page_buf.data());
    if (!read_status.ok()) {
        return read_status;
    }

    SlottedPage page(page_buf.data());
    if (!page.UpdateRecord(rid, new_record)) {
        return Status::InvalidArgument("Cannot update record in-place: insufficient space or invalid slot");
    }

    return disk_mgr_->WritePage(rid.page_id, page_buf.data());
}

Status TableHeap::DeleteRecord(const RID& rid) {
    if (!rid.IsValid()) {
        return Status::InvalidArgument("Invalid RID");
    }

    std::vector<char> page_buf(PAGE_SIZE);
    auto read_status = disk_mgr_->ReadPage(rid.page_id, page_buf.data());
    if (!read_status.ok()) {
        return read_status;
    }

    SlottedPage page(page_buf.data());
    if (!page.DeleteRecord(rid)) {
        return Status::NotFound("Record not found for deletion at " + rid.ToString());
    }

    return disk_mgr_->WritePage(rid.page_id, page_buf.data());
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

    std::vector<char> page_buf(PAGE_SIZE);
    page_id_t curr_page_id = current_rid_.page_id;
    slot_id_t curr_slot_id = current_rid_.slot_id;

    while (curr_page_id != INVALID_PAGE_ID) {
        auto status = table_heap_->GetDiskManager()->ReadPage(curr_page_id, page_buf.data());
        if (!status.ok()) {
            current_rid_ = RID(INVALID_PAGE_ID, INVALID_SLOT_ID);
            return;
        }

        SlottedPage page(page_buf.data());
        uint16_t slot_count = page.GetSlotCount();

        while (curr_slot_id < slot_count) {
            Slot s = page.GetSlot(curr_slot_id);
            if (s.size > 0) {
                // Found a valid record
                current_rid_ = RID(curr_page_id, curr_slot_id);
                current_record_ = Record(page_buf.data() + s.offset, s.size, current_rid_);
                return;
            }
            ++curr_slot_id;
        }

        // Advance to next page
        curr_page_id = page.GetNextPageId();
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

} // namespace forgedb
