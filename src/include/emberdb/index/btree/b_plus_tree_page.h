#pragma once

#include "emberdb/common/config.h"
#include <cstdint>

namespace emberdb {

enum class IndexPageType : uint32_t {
    INVALID_INDEX_PAGE = 0,
    LEAF_PAGE = 1,
    INTERNAL_PAGE = 2
};

/**
 * Base header layout for all B+ Tree index pages.
 * Resides at offset 0 of each 4096-byte index page.
 */
class BPlusTreePage {
public:
    IndexPageType GetPageType() const { return page_type_; }
    void SetPageType(IndexPageType type) { page_type_ = type; }

    bool IsLeafPage() const { return page_type_ == IndexPageType::LEAF_PAGE; }
    bool IsRootPage() const { return parent_page_id_ == INVALID_PAGE_ID; }

    int GetSize() const { return size_; }
    void SetSize(int size) { size_ = size; }
    void IncreaseSize(int amount) { size_ += amount; }

    int GetMaxSize() const { return max_size_; }
    void SetMaxSize(int max_size) { max_size_ = max_size; }
    int GetMinSize() const {
        if (IsRootPage()) {
            return IsLeafPage() ? 1 : 2;
        }
        return max_size_ / 2;
    }

    page_id_t GetParentPageId() const { return parent_page_id_; }
    void SetParentPageId(page_id_t parent_page_id) { parent_page_id_ = parent_page_id; }

    page_id_t GetPageId() const { return page_id_; }
    void SetPageId(page_id_t page_id) { page_id_ = page_id; }

    lsn_t GetLSN() const { return lsn_; }
    void SetLSN(lsn_t lsn) { lsn_ = lsn; }

protected:
    IndexPageType page_type_{IndexPageType::INVALID_INDEX_PAGE};
    lsn_t lsn_{INVALID_LSN};
    int32_t size_{0};
    int32_t max_size_{0};
    page_id_t parent_page_id_{INVALID_PAGE_ID};
    page_id_t page_id_{INVALID_PAGE_ID};
};

static_assert(sizeof(BPlusTreePage) == 32, "BPlusTreePage base header must be exactly 32 bytes");

} // namespace emberdb
