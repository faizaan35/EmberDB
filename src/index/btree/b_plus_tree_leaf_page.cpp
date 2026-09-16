#include "emberdb/index/btree/b_plus_tree_leaf_page.h"
#include <algorithm>

namespace emberdb {

void BPlusTreeLeafPage::Init(page_id_t page_id, page_id_t parent_id, int max_size) {
    SetPageType(IndexPageType::LEAF_PAGE);
    SetSize(0);
    SetMaxSize(max_size);
    SetParentPageId(parent_id);
    SetPageId(page_id);
    SetLSN(INVALID_LSN);
    next_page_id_ = INVALID_PAGE_ID;
    prev_page_id_ = INVALID_PAGE_ID;
}

int BPlusTreeLeafPage::KeyIndex(const IndexKey& key) const {
    int low = 0;
    int high = GetSize() - 1;
    int ans = GetSize();

    while (low <= high) {
        int mid = low + (high - low) / 2;
        if (array_[mid].key >= key) {
            ans = mid;
            high = mid - 1;
        } else {
            low = mid + 1;
        }
    }
    return ans;
}

bool BPlusTreeLeafPage::Lookup(const IndexKey& key, std::vector<RID>& result) const {
    int idx = KeyIndex(key);
    bool found = false;
    while (idx < GetSize() && array_[idx].key == key) {
        result.push_back(array_[idx].rid);
        found = true;
        idx++;
    }
    return found;
}

int BPlusTreeLeafPage::Insert(const IndexKey& key, const RID& value) {
    int idx = KeyIndex(key);

    // If key matches, find insertion point among duplicates by RID
    while (idx < GetSize() && array_[idx].key == key && array_[idx].rid < value) {
        idx++;
    }

    // Check for exact duplicate
    if (idx < GetSize() && array_[idx].key == key && array_[idx].rid == value) {
        return GetSize(); // Already present
    }

    // Shift elements right
    for (int i = GetSize(); i > idx; --i) {
        array_[i] = array_[i - 1];
    }
    array_[idx] = {key, value};
    IncreaseSize(1);
    return GetSize();
}

bool BPlusTreeLeafPage::Remove(const IndexKey& key, const RID& value) {
    int idx = KeyIndex(key);
    while (idx < GetSize() && array_[idx].key == key) {
        if (array_[idx].rid == value) {
            for (int i = idx; i < GetSize() - 1; ++i) {
                array_[i] = array_[i + 1];
            }
            IncreaseSize(-1);
            return true;
        }
        idx++;
    }
    return false;
}

void BPlusTreeLeafPage::Split(BPlusTreeLeafPage* recipient, const IndexKey& key, const RID& value) {
    int total = GetSize() + 1;
    std::vector<LeafEntry> temp;
    temp.reserve(total);

    int insert_idx = KeyIndex(key);
    while (insert_idx < GetSize() && array_[insert_idx].key == key && array_[insert_idx].rid < value) {
        insert_idx++;
    }

    for (int i = 0; i < insert_idx; ++i) {
        temp.push_back(array_[i]);
    }
    temp.push_back({key, value});
    for (int i = insert_idx; i < GetSize(); ++i) {
        temp.push_back(array_[i]);
    }

    int split_idx = total / 2;

    // Left node
    SetSize(split_idx);
    for (int i = 0; i < split_idx; ++i) {
        array_[i] = temp[i];
    }

    // Right node (recipient)
    int right_size = total - split_idx;
    recipient->SetSize(right_size);
    for (int i = 0; i < right_size; ++i) {
        recipient->array_[i] = temp[split_idx + i];
    }

    // Sibling pointers
    recipient->SetNextPageId(GetNextPageId());
    recipient->SetPrevPageId(GetPageId());
    SetNextPageId(recipient->GetPageId());
}

} // namespace emberdb
