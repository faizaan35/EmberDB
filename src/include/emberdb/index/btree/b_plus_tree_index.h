#pragma once

#include "emberdb/common/config.h"
#include "emberdb/common/types.h"
#include "emberdb/storage/buffer/buffer_pool_manager.h"
#include "emberdb/index/index_key.h"
#include "emberdb/index/btree/b_plus_tree_page.h"
#include "emberdb/index/btree/b_plus_tree_leaf_page.h"
#include "emberdb/index/btree/b_plus_tree_internal_page.h"
#include <string>
#include <vector>
#include <functional>
#include <mutex>

namespace emberdb {

/**
 * B+ Tree Index implementation supporting point lookups, insertions with
 * recursive splitting, range scans, and buffer-pool-backed page persistence.
 */
class BPlusTreeIndex {
public:
    using RootChangeCallback = std::function<void(page_id_t)>;

    BPlusTreeIndex(std::string name, BufferPoolManager* bpm, TypeId key_type, page_id_t root_page_id = INVALID_PAGE_ID);
    ~BPlusTreeIndex() = default;

    const std::string& GetName() const { return name_; }
    TypeId GetKeyType() const { return key_type_; }
    page_id_t GetRootPageId() const { return root_page_id_; }
    void SetRootPageId(page_id_t root_page_id) { root_page_id_ = root_page_id; }
    bool IsEmpty() const { return root_page_id_ == INVALID_PAGE_ID; }

    void SetOnRootPageChange(RootChangeCallback cb) { on_root_change_cb_ = std::move(cb); }

    // Search operations
    bool GetValue(const IndexKey& key, std::vector<RID>& result) const;
    bool GetOneValue(const IndexKey& key, RID& result) const;

    // Insertion & Deletion
    bool Insert(const IndexKey& key, const RID& rid);
    bool Remove(const IndexKey& key, const RID& rid);

    // Range scans
    std::vector<std::pair<IndexKey, RID>> ScanRange(const IndexKey& low_key, const IndexKey& high_key) const;
    std::vector<std::pair<IndexKey, RID>> ScanAll() const;

private:
    page_id_t FindLeafPage(const IndexKey& key, bool leftmost = false) const;
    void InsertIntoParent(BPlusTreePage* old_child, const IndexKey& key, BPlusTreePage* new_child);

    std::string name_;
    BufferPoolManager* bpm_;
    TypeId key_type_;
    page_id_t root_page_id_{INVALID_PAGE_ID};
    RootChangeCallback on_root_change_cb_;
    mutable std::mutex mutex_;
};

} // namespace emberdb
