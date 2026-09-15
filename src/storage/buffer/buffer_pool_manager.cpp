#include "emberdb/storage/buffer/buffer_pool_manager.h"

namespace emberdb {

BufferPoolManager::BufferPoolManager(size_t pool_size, DiskManager* disk_mgr)
    : pool_size_(pool_size), disk_mgr_(disk_mgr), pages_(pool_size) {
    replacer_ = std::make_unique<LRUReplacer>(pool_size);
    for (size_t i = 0; i < pool_size_; ++i) {
        free_list_.push_back(static_cast<frame_id_t>(i));
    }
    if (disk_mgr_) {
        disk_mgr_->SetFlushHook([this]() {
            FlushAllPages();
        });
    }
}

BufferPoolManager::~BufferPoolManager() {
    FlushAllPages();
    if (disk_mgr_) {
        disk_mgr_->SetFlushHook(nullptr);
    }
}

bool BufferPoolManager::FindAvailableFrame(frame_id_t* frame_id) {
    if (!free_list_.empty()) {
        *frame_id = free_list_.front();
        free_list_.pop_front();
        return true;
    }

    if (replacer_->Victim(frame_id)) {
        Page& victim_page = pages_[*frame_id];
        if (victim_page.IsDirty()) {
            disk_mgr_->WritePage(victim_page.GetPageId(), victim_page.GetData());
            victim_page.SetDirty(false);
        }
        page_table_.erase(victim_page.GetPageId());
        return true;
    }

    return false;
}

Page* BufferPoolManager::FetchPage(page_id_t page_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (page_id == INVALID_PAGE_ID) {
        return nullptr;
    }

    auto it = page_table_.find(page_id);
    if (it != page_table_.end()) {
        frame_id_t fid = it->second;
        pages_[fid].IncrementPinCount();
        replacer_->Pin(fid);
        return &pages_[fid];
    }

    frame_id_t fid = INVALID_FRAME_ID;
    if (!FindAvailableFrame(&fid)) {
        return nullptr;
    }

    pages_[fid].ResetMemory();
    pages_[fid].SetPageId(page_id);
    pages_[fid].SetDirty(false);
    pages_[fid].IncrementPinCount();

    auto status = disk_mgr_->ReadPage(page_id, pages_[fid].GetData());
    if (!status.ok()) {
        pages_[fid].DecrementPinCount();
        free_list_.push_back(fid);
        return nullptr;
    }

    page_table_[page_id] = fid;
    replacer_->Pin(fid);
    return &pages_[fid];
}

bool BufferPoolManager::UnpinPage(page_id_t page_id, bool is_dirty) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = page_table_.find(page_id);
    if (it == page_table_.end()) {
        return false;
    }

    frame_id_t fid = it->second;
    if (is_dirty) {
        pages_[fid].SetDirty(true);
    }

    if (pages_[fid].GetPinCount() <= 0) {
        return false;
    }

    pages_[fid].DecrementPinCount();
    if (pages_[fid].GetPinCount() == 0) {
        replacer_->Unpin(fid);
    }
    return true;
}

Page* BufferPoolManager::NewPage(page_id_t* page_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    frame_id_t fid = INVALID_FRAME_ID;
    if (!FindAvailableFrame(&fid)) {
        return nullptr;
    }

    auto alloc_res = disk_mgr_->AllocatePage();
    if (!alloc_res.ok()) {
        free_list_.push_back(fid);
        return nullptr;
    }

    page_id_t new_pid = *alloc_res;
    pages_[fid].ResetMemory();
    pages_[fid].SetPageId(new_pid);
    pages_[fid].SetDirty(true);
    pages_[fid].IncrementPinCount();

    page_table_[new_pid] = fid;
    replacer_->Pin(fid);

    *page_id = new_pid;
    return &pages_[fid];
}

bool BufferPoolManager::FlushPage(page_id_t page_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = page_table_.find(page_id);
    if (it == page_table_.end()) {
        return false;
    }

    frame_id_t fid = it->second;
    disk_mgr_->WritePage(page_id, pages_[fid].GetData());
    pages_[fid].SetDirty(false);
    return true;
}

void BufferPoolManager::FlushAllPages() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [pid, fid] : page_table_) {
        if (pages_[fid].IsDirty()) {
            disk_mgr_->WritePage(pid, pages_[fid].GetData());
            pages_[fid].SetDirty(false);
        }
    }
}

bool BufferPoolManager::DeletePage(page_id_t page_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = page_table_.find(page_id);
    if (it == page_table_.end()) {
        return true;
    }

    frame_id_t fid = it->second;
    if (pages_[fid].GetPinCount() > 0) {
        return false;
    }

    if (pages_[fid].IsDirty()) {
        disk_mgr_->WritePage(page_id, pages_[fid].GetData());
        pages_[fid].SetDirty(false);
    }

    page_table_.erase(it);
    replacer_->Pin(fid);
    pages_[fid].ResetMemory();
    free_list_.push_back(fid);
    return true;
}

bool BufferPoolManager::IsPageInPool(page_id_t page_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return page_table_.find(page_id) != page_table_.end();
}

int BufferPoolManager::GetPinCount(page_id_t page_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = page_table_.find(page_id);
    if (it == page_table_.end()) {
        return -1;
    }
    return pages_[it->second].GetPinCount();
}

} // namespace emberdb
