#pragma once

#include "emberdb/common/config.h"
#include "emberdb/storage/disk/disk_manager.h"
#include "emberdb/storage/page/page.h"
#include "emberdb/storage/buffer/lru_replacer.h"
#include <vector>
#include <unordered_map>
#include <list>
#include <mutex>
#include <memory>

namespace emberdb {

/**
 * BufferPoolManager manages the paging of database pages between memory and disk storage.
 */
class BufferPoolManager {
public:
    BufferPoolManager(size_t pool_size, DiskManager* disk_mgr);
    ~BufferPoolManager();

    // Core page management
    Page* FetchPage(page_id_t page_id);
    bool UnpinPage(page_id_t page_id, bool is_dirty);
    bool FlushPage(page_id_t page_id);
    Page* NewPage(page_id_t* page_id);
    bool DeletePage(page_id_t page_id);
    void FlushAllPages();

    // Inspection
    size_t GetPoolSize() const { return pool_size_; }
    DiskManager* GetDiskManager() const { return disk_mgr_; }
    bool IsPageInPool(page_id_t page_id);
    int GetPinCount(page_id_t page_id);

private:
    bool FindAvailableFrame(frame_id_t* frame_id);

    size_t pool_size_;
    DiskManager* disk_mgr_;
    std::vector<Page> pages_;
    std::unordered_map<page_id_t, frame_id_t> page_table_;
    std::unique_ptr<Replacer> replacer_;
    std::list<frame_id_t> free_list_;
    std::mutex mutex_;
};

} // namespace emberdb
