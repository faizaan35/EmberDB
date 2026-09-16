#include "emberdb/index/btree/b_plus_tree_index.h"
#include <iostream>

namespace emberdb {

BPlusTreeIndex::BPlusTreeIndex(std::string name, BufferPoolManager* bpm, TypeId key_type, page_id_t root_page_id)
    : name_(std::move(name)), bpm_(bpm), key_type_(key_type), root_page_id_(root_page_id) {}

page_id_t BPlusTreeIndex::FindLeafPage(const IndexKey& key, bool leftmost) const {
    if (root_page_id_ == INVALID_PAGE_ID) {
        return INVALID_PAGE_ID;
    }

    page_id_t curr_pid = root_page_id_;
    Page* curr_page = bpm_->FetchPage(curr_pid);
    if (!curr_page) return INVALID_PAGE_ID;

    auto* tree_page = reinterpret_cast<BPlusTreePage*>(curr_page->GetData());

    while (!tree_page->IsLeafPage()) {
        auto* internal = reinterpret_cast<BPlusTreeInternalPage*>(curr_page->GetData());
        page_id_t next_pid = leftmost ? internal->ValueAt(0) : internal->Lookup(key);

        bpm_->UnpinPage(curr_pid, false);
        curr_pid = next_pid;
        curr_page = bpm_->FetchPage(curr_pid);
        if (!curr_page) return INVALID_PAGE_ID;
        tree_page = reinterpret_cast<BPlusTreePage*>(curr_page->GetData());
    }

    bpm_->UnpinPage(curr_pid, false);
    return curr_pid;
}

bool BPlusTreeIndex::GetValue(const IndexKey& key, std::vector<RID>& result) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (root_page_id_ == INVALID_PAGE_ID) {
        return false;
    }

    page_id_t leaf_pid = FindLeafPage(key);
    if (leaf_pid == INVALID_PAGE_ID) {
        return false;
    }

    Page* leaf_p = bpm_->FetchPage(leaf_pid);
    if (!leaf_p) return false;

    auto* leaf = reinterpret_cast<BPlusTreeLeafPage*>(leaf_p->GetData());
    bool found = leaf->Lookup(key, result);
    bpm_->UnpinPage(leaf_pid, false);
    return found;
}

bool BPlusTreeIndex::GetOneValue(const IndexKey& key, RID& result) const {
    std::vector<RID> vec;
    if (GetValue(key, vec) && !vec.empty()) {
        result = vec[0];
        return true;
    }
    return false;
}

bool BPlusTreeIndex::Insert(const IndexKey& key, const RID& rid) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (root_page_id_ == INVALID_PAGE_ID) {
        Page* root_page = bpm_->NewPage(&root_page_id_);
        if (!root_page) return false;

        auto* leaf = reinterpret_cast<BPlusTreeLeafPage*>(root_page->GetData());
        leaf->Init(root_page_id_, INVALID_PAGE_ID, LEAF_MAX_SIZE);
        leaf->Insert(key, rid);
        bpm_->UnpinPage(root_page_id_, true);

        if (on_root_change_cb_) {
            on_root_change_cb_(root_page_id_);
        }
        return true;
    }

    page_id_t leaf_pid = FindLeafPage(key);
    if (leaf_pid == INVALID_PAGE_ID) return false;

    Page* leaf_p = bpm_->FetchPage(leaf_pid);
    if (!leaf_p) return false;

    auto* leaf = reinterpret_cast<BPlusTreeLeafPage*>(leaf_p->GetData());

    if (leaf->GetSize() < leaf->GetMaxSize()) {
        leaf->Insert(key, rid);
        bpm_->UnpinPage(leaf_pid, true);
        return true;
    }

    // Leaf Split
    page_id_t new_leaf_pid = INVALID_PAGE_ID;
    Page* new_leaf_p = bpm_->NewPage(&new_leaf_pid);
    if (!new_leaf_p) {
        bpm_->UnpinPage(leaf_pid, false);
        return false;
    }

    auto* new_leaf = reinterpret_cast<BPlusTreeLeafPage*>(new_leaf_p->GetData());
    new_leaf->Init(new_leaf_pid, leaf->GetParentPageId(), leaf->GetMaxSize());

    page_id_t old_next_pid = leaf->GetNextPageId();
    leaf->Split(new_leaf, key, rid);

    if (old_next_pid != INVALID_PAGE_ID) {
        Page* next_p = bpm_->FetchPage(old_next_pid);
        if (next_p) {
            auto* next_leaf = reinterpret_cast<BPlusTreeLeafPage*>(next_p->GetData());
            next_leaf->SetPrevPageId(new_leaf_pid);
            bpm_->UnpinPage(old_next_pid, true);
        }
    }

    IndexKey push_up_key = new_leaf->KeyAt(0);
    InsertIntoParent(leaf, push_up_key, new_leaf);

    bpm_->UnpinPage(leaf_pid, true);
    bpm_->UnpinPage(new_leaf_pid, true);
    return true;
}

bool BPlusTreeIndex::Remove(const IndexKey& key, const RID& rid) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (IsEmpty()) return false;

    page_id_t leaf_pid = FindLeafPage(key);
    if (leaf_pid == INVALID_PAGE_ID) return false;

    Page* p = bpm_->FetchPage(leaf_pid);
    if (!p) return false;

    auto* leaf = reinterpret_cast<BPlusTreeLeafPage*>(p->GetData());
    bool removed = leaf->Remove(key, rid);
    bpm_->UnpinPage(leaf_pid, removed);
    return removed;
}

void BPlusTreeIndex::InsertIntoParent(BPlusTreePage* old_child, const IndexKey& key, BPlusTreePage* new_child) {
    if (old_child->IsRootPage()) {
        page_id_t new_root_pid = INVALID_PAGE_ID;
        Page* new_root_p = bpm_->NewPage(&new_root_pid);
        if (!new_root_p) return;

        auto* new_root = reinterpret_cast<BPlusTreeInternalPage*>(new_root_p->GetData());
        new_root->Init(new_root_pid, INVALID_PAGE_ID, INTERNAL_MAX_SIZE);
        new_root->Populate(old_child->GetPageId(), key, new_child->GetPageId());

        old_child->SetParentPageId(new_root_pid);
        new_child->SetParentPageId(new_root_pid);
        root_page_id_ = new_root_pid;

        bpm_->UnpinPage(new_root_pid, true);

        if (on_root_change_cb_) {
            on_root_change_cb_(root_page_id_);
        }
        return;
    }

    page_id_t parent_pid = old_child->GetParentPageId();
    Page* parent_p = bpm_->FetchPage(parent_pid);
    if (!parent_p) return;

    auto* parent = reinterpret_cast<BPlusTreeInternalPage*>(parent_p->GetData());

    if (parent->GetSize() < parent->GetMaxSize()) {
        parent->InsertNodeAfter(old_child->GetPageId(), key, new_child->GetPageId());
        new_child->SetParentPageId(parent_pid);
        bpm_->UnpinPage(parent_pid, true);
        return;
    }

    // Internal Split
    page_id_t new_parent_pid = INVALID_PAGE_ID;
    Page* new_parent_p = bpm_->NewPage(&new_parent_pid);
    if (!new_parent_p) {
        bpm_->UnpinPage(parent_pid, false);
        return;
    }

    auto* new_parent = reinterpret_cast<BPlusTreeInternalPage*>(new_parent_p->GetData());
    new_parent->Init(new_parent_pid, parent->GetParentPageId(), parent->GetMaxSize());

    IndexKey push_up;
    new_child->SetParentPageId(parent_pid);
    parent->Split(new_parent, old_child->GetPageId(), key, new_child->GetPageId(), push_up);

    // Update parent pointers of child pages belonging to new_parent
    for (int i = 0; i < new_parent->GetSize(); ++i) {
        page_id_t c_pid = new_parent->ValueAt(i);
        Page* cp = bpm_->FetchPage(c_pid);
        if (cp) {
            auto* cp_node = reinterpret_cast<BPlusTreePage*>(cp->GetData());
            cp_node->SetParentPageId(new_parent_pid);
            bpm_->UnpinPage(c_pid, true);
        }
    }

    InsertIntoParent(parent, push_up, new_parent);

    bpm_->UnpinPage(parent_pid, true);
    bpm_->UnpinPage(new_parent_pid, true);
}

std::vector<std::pair<IndexKey, RID>> BPlusTreeIndex::ScanRange(const IndexKey& low_key, const IndexKey& high_key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::pair<IndexKey, RID>> results;

    if (root_page_id_ == INVALID_PAGE_ID) {
        return results;
    }

    page_id_t curr_pid = FindLeafPage(low_key);
    if (curr_pid == INVALID_PAGE_ID) {
        return results;
    }

    Page* curr_page = bpm_->FetchPage(curr_pid);
    if (!curr_page) return results;

    auto* leaf = reinterpret_cast<BPlusTreeLeafPage*>(curr_page->GetData());
    int idx = leaf->KeyIndex(low_key);

    while (true) {
        while (idx < leaf->GetSize()) {
            if (leaf->KeyAt(idx) > high_key) {
                bpm_->UnpinPage(curr_pid, false);
                return results;
            }
            results.push_back({leaf->KeyAt(idx), leaf->ValueAt(idx)});
            idx++;
        }

        page_id_t next_pid = leaf->GetNextPageId();
        bpm_->UnpinPage(curr_pid, false);

        if (next_pid == INVALID_PAGE_ID) break;

        curr_pid = next_pid;
        curr_page = bpm_->FetchPage(curr_pid);
        if (!curr_page) break;

        leaf = reinterpret_cast<BPlusTreeLeafPage*>(curr_page->GetData());
        idx = 0;
    }

    return results;
}

std::vector<std::pair<IndexKey, RID>> BPlusTreeIndex::ScanAll() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::pair<IndexKey, RID>> results;

    if (root_page_id_ == INVALID_PAGE_ID) {
        return results;
    }

    page_id_t curr_pid = FindLeafPage(IndexKey{}, /*leftmost=*/true);
    if (curr_pid == INVALID_PAGE_ID) {
        return results;
    }

    Page* curr_page = bpm_->FetchPage(curr_pid);
    if (!curr_page) return results;

    auto* leaf = reinterpret_cast<BPlusTreeLeafPage*>(curr_page->GetData());

    while (true) {
        for (int i = 0; i < leaf->GetSize(); ++i) {
            results.push_back({leaf->KeyAt(i), leaf->ValueAt(i)});
        }

        page_id_t next_pid = leaf->GetNextPageId();
        bpm_->UnpinPage(curr_pid, false);

        if (next_pid == INVALID_PAGE_ID) break;

        curr_pid = next_pid;
        curr_page = bpm_->FetchPage(curr_pid);
        if (!curr_page) break;

        leaf = reinterpret_cast<BPlusTreeLeafPage*>(curr_page->GetData());
    }

    return results;
}

} // namespace emberdb
