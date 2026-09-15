#include "emberdb/storage/page/page.h"

namespace emberdb {

Page::Page() {
    ResetMemory();
}

lsn_t Page::GetLSN() const {
    // LSN is located at byte offset 4 within the page header
    lsn_t lsn = INVALID_LSN;
    std::memcpy(&lsn, data_ + sizeof(page_id_t), sizeof(lsn_t));
    return lsn;
}

void Page::SetLSN(lsn_t lsn) {
    std::memcpy(data_ + sizeof(page_id_t), &lsn, sizeof(lsn_t));
}

void Page::ResetMemory() {
    std::memset(data_, 0, PAGE_SIZE);
    page_id_ = INVALID_PAGE_ID;
    pin_count_ = 0;
    is_dirty_ = false;
}

} // namespace emberdb
