#include "emberdb/index/btree/b_plus_tree_internal_page.h"
#include <vector>
#include <stdexcept>

namespace emberdb {

void BPlusTreeInternalPage::Init(page_id_t page_id, page_id_t parent_id, int max_size) {
    SetPageType(IndexPageType::INTERNAL_PAGE);
    SetSize(0);
    SetMaxSize(max_size);
    SetParentPageId(parent_id);
    SetPageId(page_id);
    SetLSN(INVALID_LSN);
}

page_id_t BPlusTreeInternalPage::Lookup(const IndexKey& key) const {
    if (GetSize() <= 0) {
        return INVALID_PAGE_ID;
    }

    // Binary search for largest index where array_[index].key <= key
    // array_[0] has no valid key, so valid keys start at index 1
    int low = 1;
    int high = GetSize() - 1;
    int ans = 0;

    while (low <= high) {
        int mid = low + (high - low) / 2;
        if (array_[mid].key <= key) {
            ans = mid;
            low = mid + 1;
        } else {
            high = mid - 1;
        }
    }
    return array_[ans].page_id;
}

void BPlusTreeInternalPage::Populate(page_id_t value0, const IndexKey& key1, page_id_t value1) {
    array_[0].page_id = value0;
    array_[1].key = key1;
    array_[1].page_id = value1;
    SetSize(2);
}

void BPlusTreeInternalPage::InsertNodeAfter(page_id_t old_value, const IndexKey& new_key, page_id_t new_value) {
    int idx = 0;
    while (idx < GetSize() && array_[idx].page_id != old_value) {
        idx++;
    }
    if (idx >= GetSize()) {
        idx = GetSize() - 1;
    }
    int insert_idx = idx + 1;

    for (int i = GetSize(); i > insert_idx; --i) {
        array_[i] = array_[i - 1];
    }
    array_[insert_idx].key = new_key;
    array_[insert_idx].page_id = new_value;
    IncreaseSize(1);
}

void BPlusTreeInternalPage::Split(BPlusTreeInternalPage* recipient,
                                  page_id_t old_value,
                                  const IndexKey& new_key,
                                  page_id_t new_value,
                                  IndexKey& push_up_key) {
    int total = GetSize() + 1;
    std::vector<InternalEntry> temp;
    temp.reserve(total);

    int idx = 0;
    while (idx < GetSize() && array_[idx].page_id != old_value) {
        idx++;
    }
    if (idx >= GetSize()) {
        idx = GetSize() - 1;
    }
    int insert_idx = idx + 1;

    for (int i = 0; i < insert_idx; ++i) {
        temp.push_back(array_[i]);
    }
    InternalEntry new_entry;
    new_entry.key = new_key;
    new_entry.page_id = new_value;
    temp.push_back(new_entry);
    for (int i = insert_idx; i < GetSize(); ++i) {
        temp.push_back(array_[i]);
    }

    int split_idx = total / 2;

    // Left node retains [0 .. split_idx - 1]
    SetSize(split_idx);
    for (int i = 0; i < split_idx; ++i) {
        array_[i] = temp[i];
    }

    // Key at split_idx is pushed up to the parent
    push_up_key = temp[split_idx].key;

    // Right node (recipient) receives entry 0 with pointer from temp[split_idx].page_id
    recipient->array_[0].page_id = temp[split_idx].page_id;

    // And entries [split_idx + 1 .. total - 1]
    int right_entries = total - split_idx - 1;
    for (int i = 0; i < right_entries; ++i) {
        recipient->array_[i + 1] = temp[split_idx + 1 + i];
    }
    recipient->SetSize(right_entries + 1);
}

} // namespace emberdb
