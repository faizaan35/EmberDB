#pragma once

#include "emberdb/index/btree/b_plus_tree_page.h"
#include "emberdb/index/index_key.h"

namespace emberdb {

constexpr int INTERNAL_MAX_SIZE = 29;

struct InternalEntry {
    IndexKey key;
    page_id_t page_id{INVALID_PAGE_ID};
    uint32_t padding{0};
};

static_assert(sizeof(InternalEntry) == 136, "InternalEntry must be exactly 136 bytes");

/**
 * On-disk page layout for B+ Tree internal nodes.
 * Entry 0 has a dummy key and points to the child subtree for keys < array_[1].key.
 * Entries 1..size-1 store (key, child_page_id) for keys >= array_[i].key.
 */
class BPlusTreeInternalPage : public BPlusTreePage {
public:
    void Init(page_id_t page_id, page_id_t parent_id = INVALID_PAGE_ID, int max_size = INTERNAL_MAX_SIZE);

    const IndexKey& KeyAt(int index) const { return array_[index].key; }
    void SetKeyAt(int index, const IndexKey& key) { array_[index].key = key; }

    page_id_t ValueAt(int index) const { return array_[index].page_id; }
    void SetValueAt(int index, page_id_t value) { array_[index].page_id = value; }

    const InternalEntry& EntryAt(int index) const { return array_[index]; }

    page_id_t Lookup(const IndexKey& key) const;
    void Populate(page_id_t value0, const IndexKey& key1, page_id_t value1);
    void InsertNodeAfter(page_id_t old_value, const IndexKey& new_key, page_id_t new_value);
    void Split(BPlusTreeInternalPage* recipient, page_id_t old_value, const IndexKey& new_key, page_id_t new_value, IndexKey& push_up_key);

private:
    InternalEntry array_[INTERNAL_MAX_SIZE];
};

static_assert(sizeof(BPlusTreeInternalPage) <= PAGE_SIZE, "BPlusTreeInternalPage exceeds 4096-byte PAGE_SIZE");

} // namespace emberdb
