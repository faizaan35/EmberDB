#include "emberdb/storage/buffer/lru_replacer.h"

namespace emberdb {

LRUReplacer::LRUReplacer(size_t num_pages) : capacity_(num_pages) {}

bool LRUReplacer::Victim(frame_id_t* frame_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (lru_list_.empty()) {
        return false;
    }

    *frame_id = lru_list_.front();
    lru_map_.erase(*frame_id);
    lru_list_.pop_front();
    return true;
}

void LRUReplacer::Pin(frame_id_t frame_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = lru_map_.find(frame_id);
    if (it != lru_map_.end()) {
        lru_list_.erase(it->second);
        lru_map_.erase(it);
    }
}

void LRUReplacer::Unpin(frame_id_t frame_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = lru_map_.find(frame_id);
    if (it != lru_map_.end()) {
        return; // Already unpinned
    }

    lru_list_.push_back(frame_id);
    lru_map_[frame_id] = std::prev(lru_list_.end());
}

size_t LRUReplacer::Size() {
    std::lock_guard<std::mutex> lock(mutex_);
    return lru_list_.size();
}

} // namespace emberdb
