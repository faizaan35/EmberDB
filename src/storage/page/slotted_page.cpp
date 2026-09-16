#include "emberdb/storage/page/slotted_page.h"

namespace emberdb {

// Offsets within page header
constexpr size_t OFFSET_PAGE_ID = 0;             // 4 bytes
constexpr size_t OFFSET_LSN = 4;                 // 8 bytes
constexpr size_t OFFSET_PREV_PAGE_ID = 12;       // 4 bytes
constexpr size_t OFFSET_NEXT_PAGE_ID = 16;       // 4 bytes
constexpr size_t OFFSET_SLOT_COUNT = 20;         // 2 bytes
constexpr size_t OFFSET_FREE_SPACE_PTR = 22;     // 2 bytes

SlottedPage::SlottedPage(Page* page) : data_(page->GetData()) {}

SlottedPage::SlottedPage(char* page_data) : data_(page_data) {}

void SlottedPage::Init(page_id_t page_id, page_id_t prev_page_id, page_id_t next_page_id) {
    std::memset(data_, 0, PAGE_SIZE);
    SetPageId(page_id);
    SetLSN(INVALID_LSN);
    SetPrevPageId(prev_page_id);
    SetNextPageId(next_page_id);
    SetSlotCount(0);
    SetFreeSpacePointer(static_cast<uint16_t>(PAGE_SIZE));
}

page_id_t SlottedPage::GetPageId() const {
    page_id_t id = INVALID_PAGE_ID;
    std::memcpy(&id, data_ + OFFSET_PAGE_ID, sizeof(id));
    return id;
}

void SlottedPage::SetPageId(page_id_t page_id) {
    std::memcpy(data_ + OFFSET_PAGE_ID, &page_id, sizeof(page_id));
}

lsn_t SlottedPage::GetLSN() const {
    lsn_t lsn = INVALID_LSN;
    std::memcpy(&lsn, data_ + OFFSET_LSN, sizeof(lsn));
    return lsn;
}

void SlottedPage::SetLSN(lsn_t lsn) {
    std::memcpy(data_ + OFFSET_LSN, &lsn, sizeof(lsn));
}

page_id_t SlottedPage::GetPrevPageId() const {
    page_id_t id = INVALID_PAGE_ID;
    std::memcpy(&id, data_ + OFFSET_PREV_PAGE_ID, sizeof(id));
    return id;
}

void SlottedPage::SetPrevPageId(page_id_t prev_page_id) {
    std::memcpy(data_ + OFFSET_PREV_PAGE_ID, &prev_page_id, sizeof(prev_page_id));
}

page_id_t SlottedPage::GetNextPageId() const {
    page_id_t id = INVALID_PAGE_ID;
    std::memcpy(&id, data_ + OFFSET_NEXT_PAGE_ID, sizeof(id));
    return id;
}

void SlottedPage::SetNextPageId(page_id_t next_page_id) {
    std::memcpy(data_ + OFFSET_NEXT_PAGE_ID, &next_page_id, sizeof(next_page_id));
}

uint16_t SlottedPage::GetSlotCount() const {
    uint16_t count = 0;
    std::memcpy(&count, data_ + OFFSET_SLOT_COUNT, sizeof(count));
    return count;
}

void SlottedPage::SetSlotCount(uint16_t count) {
    std::memcpy(data_ + OFFSET_SLOT_COUNT, &count, sizeof(count));
}

uint16_t SlottedPage::GetFreeSpacePointer() const {
    uint16_t ptr = 0;
    std::memcpy(&ptr, data_ + OFFSET_FREE_SPACE_PTR, sizeof(ptr));
    return ptr;
}

void SlottedPage::SetFreeSpacePointer(uint16_t ptr) {
    std::memcpy(data_ + OFFSET_FREE_SPACE_PTR, &ptr, sizeof(ptr));
}

size_t SlottedPage::GetFreeSpace() const {
    uint16_t slot_count = GetSlotCount();
    uint16_t free_ptr = GetFreeSpacePointer();
    size_t slot_dir_end = PAGE_HEADER_SIZE + (slot_count * sizeof(Slot));
    if (free_ptr < slot_dir_end) return 0;
    return free_ptr - slot_dir_end;
}

Slot SlottedPage::GetSlot(slot_id_t slot_id) const {
    Slot slot{0, 0};
    if (slot_id < 0 || slot_id >= GetSlotCount()) {
        return slot;
    }
    size_t offset = PAGE_HEADER_SIZE + (slot_id * sizeof(Slot));
    std::memcpy(&slot, data_ + offset, sizeof(Slot));
    return slot;
}

void SlottedPage::SetSlot(slot_id_t slot_id, const Slot& slot) {
    size_t offset = PAGE_HEADER_SIZE + (slot_id * sizeof(Slot));
    std::memcpy(data_ + offset, &slot, sizeof(Slot));
}

bool SlottedPage::InsertRecord(const Record& record, RID& rid) {
    uint32_t rec_len = record.GetLength();
    if (rec_len == 0 || rec_len > PAGE_SIZE - PAGE_HEADER_SIZE - sizeof(Slot)) {
        return false;
    }

    uint16_t slot_count = GetSlotCount();
    uint16_t free_ptr = GetFreeSpacePointer();

    // Look for an existing tombstoned slot
    slot_id_t target_slot_id = INVALID_SLOT_ID;
    for (slot_id_t i = 0; i < slot_count; ++i) {
        Slot s = GetSlot(i);
        if (s.size == 0) {
            target_slot_id = i;
            break;
        }
    }

    size_t required_space = rec_len;
    if (target_slot_id == INVALID_SLOT_ID) {
        required_space += sizeof(Slot);
    }

    if (GetFreeSpace() < required_space) {
        return false;
    }

    // Allocate record from the bottom of the page
    uint16_t new_record_offset = static_cast<uint16_t>(free_ptr - rec_len);
    std::memcpy(data_ + new_record_offset, record.GetData(), rec_len);
    SetFreeSpacePointer(new_record_offset);

    if (target_slot_id == INVALID_SLOT_ID) {
        target_slot_id = slot_count;
        SetSlotCount(slot_count + 1);
    }

    Slot new_slot{new_record_offset, static_cast<uint16_t>(rec_len)};
    SetSlot(target_slot_id, new_slot);

    rid = RID(GetPageId(), target_slot_id);
    return true;
}

bool SlottedPage::GetRecord(const RID& rid, Record& record) const {
    if (rid.page_id != GetPageId()) return false;
    if (rid.slot_id < 0 || rid.slot_id >= GetSlotCount()) return false;

    Slot s = GetSlot(rid.slot_id);
    if (s.size == 0) {
        return false; // Deleted
    }

    record = Record(data_ + s.offset, s.size, rid);
    return true;
}

bool SlottedPage::UpdateRecord(const RID& rid, const Record& new_record) {
    if (rid.page_id != GetPageId()) return false;
    if (rid.slot_id < 0 || rid.slot_id >= GetSlotCount()) return false;

    Slot s = GetSlot(rid.slot_id);
    if (s.size == 0) return false;

    uint32_t new_len = new_record.GetLength();
    if (new_len <= s.size) {
        // Can overwrite in place
        std::memcpy(data_ + s.offset, new_record.GetData(), new_len);
        s.size = static_cast<uint16_t>(new_len);
        SetSlot(rid.slot_id, s);
        return true;
    }

    // If larger, need additional space from free space
    if (GetFreeSpace() < new_len) {
        return false;
    }

    uint16_t free_ptr = GetFreeSpacePointer();
    uint16_t new_offset = static_cast<uint16_t>(free_ptr - new_len);
    std::memcpy(data_ + new_offset, new_record.GetData(), new_len);
    SetFreeSpacePointer(new_offset);

    s.offset = new_offset;
    s.size = static_cast<uint16_t>(new_len);
    SetSlot(rid.slot_id, s);
    return true;
}

bool SlottedPage::DeleteRecord(const RID& rid) {
    if (rid.page_id != GetPageId()) return false;
    if (rid.slot_id < 0 || rid.slot_id >= GetSlotCount()) return false;

    Slot s = GetSlot(rid.slot_id);
    if (s.size == 0) return false;

    s.size = 0; // Mark deleted / tombstone
    SetSlot(rid.slot_id, s);
    return true;
}

bool SlottedPage::RollbackDelete(const RID& rid, const Record& old_record) {
    if (rid.page_id != GetPageId()) return false;
    if (rid.slot_id < 0 || rid.slot_id >= GetSlotCount()) return false;

    Slot s = GetSlot(rid.slot_id);
    if (s.size != 0) {
        return UpdateRecord(rid, old_record);
    }

    uint32_t rec_len = old_record.GetLength();
    if (rec_len == 0 || rec_len > PAGE_SIZE - PAGE_HEADER_SIZE - sizeof(Slot)) {
        return false;
    }

    if (GetFreeSpace() < rec_len) {
        return false;
    }

    uint16_t free_ptr = GetFreeSpacePointer();
    uint16_t new_offset = static_cast<uint16_t>(free_ptr - rec_len);
    std::memcpy(data_ + new_offset, old_record.GetData(), rec_len);
    SetFreeSpacePointer(new_offset);

    s.offset = new_offset;
    s.size = static_cast<uint16_t>(rec_len);
    SetSlot(rid.slot_id, s);
    return true;
}

bool SlottedPage::RedoInsert(const RID& rid, const Record& record) {
    if (rid.page_id != GetPageId()) return false;
    uint32_t rec_len = record.GetLength();
    if (rec_len == 0 || rec_len > PAGE_SIZE - PAGE_HEADER_SIZE - sizeof(Slot)) {
        return false;
    }

    uint16_t slot_count = GetSlotCount();
    if (rid.slot_id < slot_count) {
        Slot s = GetSlot(rid.slot_id);
        if (s.size != 0) {
            return UpdateRecord(rid, record);
        }
    } else {
        uint16_t needed_slots = static_cast<uint16_t>(rid.slot_id + 1 - slot_count);
        if (GetFreeSpace() < needed_slots * sizeof(Slot) + rec_len) {
            return false;
        }
        SetSlotCount(static_cast<uint16_t>(rid.slot_id + 1));
    }

    if (GetFreeSpace() < rec_len) {
        return false;
    }

    uint16_t free_ptr = GetFreeSpacePointer();
    uint16_t new_offset = static_cast<uint16_t>(free_ptr - rec_len);
    std::memcpy(data_ + new_offset, record.GetData(), rec_len);
    SetFreeSpacePointer(new_offset);

    Slot s;
    s.offset = new_offset;
    s.size = static_cast<uint16_t>(rec_len);
    SetSlot(rid.slot_id, s);
    return true;
}

} // namespace emberdb
