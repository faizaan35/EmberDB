#pragma once

#include "forgedb/storage/page/page.h"
#include "forgedb/storage/record/record.h"

namespace forgedb {

struct Slot {
    uint16_t offset{0};
    uint16_t size{0}; // 0 denotes deleted / tombstone
};

/**
 * SlottedPage implements variable-length record management inside a fixed 4096-byte Page.
 */
class SlottedPage {
public:
    explicit SlottedPage(Page* page);
    explicit SlottedPage(char* page_data);

    void Init(page_id_t page_id, page_id_t prev_page_id = INVALID_PAGE_ID, page_id_t next_page_id = INVALID_PAGE_ID);

    // Header getters / setters
    page_id_t GetPageId() const;
    void SetPageId(page_id_t page_id);

    lsn_t GetLSN() const;
    void SetLSN(lsn_t lsn);

    page_id_t GetPrevPageId() const;
    void SetPrevPageId(page_id_t prev_page_id);

    page_id_t GetNextPageId() const;
    void SetNextPageId(page_id_t next_page_id);

    uint16_t GetSlotCount() const;
    uint16_t GetFreeSpacePointer() const;

    size_t GetFreeSpace() const;

    // Record operations
    bool InsertRecord(const Record& record, RID& rid);
    bool GetRecord(const RID& rid, Record& record) const;
    bool UpdateRecord(const RID& rid, const Record& new_record);
    bool DeleteRecord(const RID& rid);

    Slot GetSlot(slot_id_t slot_id) const;

private:
    char* data_;

    void SetSlotCount(uint16_t count);
    void SetFreeSpacePointer(uint16_t ptr);
    void SetSlot(slot_id_t slot_id, const Slot& slot);
};

} // namespace forgedb
