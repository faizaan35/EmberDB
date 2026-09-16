#pragma once

#include "emberdb/common/config.h"
#include "emberdb/common/rw_latch.h"
#include <cstring>

namespace emberdb {

// Size of the raw page header on disk
constexpr size_t PAGE_HEADER_SIZE = 24;

/**
 * In-memory representation of a physical database page.
 * Provides the fixed-size 4096-byte memory buffer and tracking metadata for the buffer pool.
 */
class Page {
public:
    Page();
    ~Page() = default;

    // Buffer access
    char* GetData() { return data_; }
    const char* GetData() const { return data_; }

    // Page identification
    page_id_t GetPageId() const { return page_id_; }
    void SetPageId(page_id_t page_id) { page_id_ = page_id; }

    // Pin count management
    int GetPinCount() const { return pin_count_; }
    void IncrementPinCount() { ++pin_count_; }
    void DecrementPinCount() {
        if (pin_count_ > 0) {
            --pin_count_;
        }
    }

    // Dirty flag
    bool IsDirty() const { return is_dirty_; }
    void SetDirty(bool is_dirty) { is_dirty_ = is_dirty; }

    // LSN
    lsn_t GetLSN() const;
    void SetLSN(lsn_t lsn);

    // Reset page contents
    void ResetMemory();

    // RW Latches
    void WLatch() { rwlock_.lock(); }
    void WUnlatch() { rwlock_.unlock(); }
    void RLatch() { rwlock_.lock_shared(); }
    void RUnlatch() { rwlock_.unlock_shared(); }

private:
    char data_[PAGE_SIZE];
    page_id_t page_id_{INVALID_PAGE_ID};
    int pin_count_{0};
    bool is_dirty_{false};
    mutable ReaderWriterLatch rwlock_;
};

} // namespace emberdb
