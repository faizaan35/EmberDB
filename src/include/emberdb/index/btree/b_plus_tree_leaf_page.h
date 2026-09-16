#pragma once

#include "emberdb/index/btree/b_plus_tree_page.h"
#include "emberdb/index/index_key.h"
#include <vector>

namespace emberdb {

constexpr int LEAF_MAX_SIZE = 29;

struct LeafEntry {
    IndexKey key;
    RID rid;

    bool operator<(const LeafEntry& o) const {
        if (key == o.key) {
            return rid < o.rid;
        }
        return key < o.key;
    }
};

static_assert(sizeof(LeafEntry) == 136, "LeafEntry must be exactly 136 bytes");

/**
 * On-disk page layout for B+ Tree leaf nodes.
 * Stores sorted (IndexKey, RID) pairs with bidirectional sibling links.
 */
class BPlusTreeLeafPage : public BPlusTreePage {
public:
    void Init(page_id_t page_id, page_id_t parent_id = INVALID_PAGE_ID, int max_size = LEAF_MAX_SIZE);

    page_id_t GetNextPageId() const { return next_page_id_; }
    void SetNextPageId(page_id_t next_page_id) { next_page_id_ = next_page_id; }

    page_id_t GetPrevPageId() const { return prev_page_id_; }
    void SetPrevPageId(page_id_t prev_page_id) { prev_page_id_ = prev_page_id; }

    const IndexKey& KeyAt(int index) const { return array_[index].key; }
    const RID& ValueAt(int index) const { return array_[index].rid; }
    const LeafEntry& EntryAt(int index) const { return array_[index]; }

    int KeyIndex(const IndexKey& key) const;
    bool Lookup(const IndexKey& key, std::vector<RID>& result) const;
    int Insert(const IndexKey& key, const RID& value);
    void Split(BPlusTreeLeafPage* recipient, const IndexKey& key, const RID& value);

private:
    page_id_t next_page_id_{INVALID_PAGE_ID};
    page_id_t prev_page_id_{INVALID_PAGE_ID};
    LeafEntry array_[LEAF_MAX_SIZE];
};

static_assert(sizeof(BPlusTreeLeafPage) <= PAGE_SIZE, "BPlusTreeLeafPage exceeds 4096-byte PAGE_SIZE");

} // namespace emberdb
