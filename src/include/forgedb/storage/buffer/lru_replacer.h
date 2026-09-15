#pragma once

#include "forgedb/storage/buffer/replacer.h"
#include <list>
#include <unordered_map>
#include <mutex>

namespace forgedb {

class LRUReplacer : public Replacer {
public:
    explicit LRUReplacer(size_t num_pages);
    ~LRUReplacer() override = default;

    bool Victim(frame_id_t* frame_id) override;
    void Pin(frame_id_t frame_id) override;
    void Unpin(frame_id_t frame_id) override;
    size_t Size() override;

private:
    size_t capacity_;
    std::list<frame_id_t> lru_list_;
    std::unordered_map<frame_id_t, std::list<frame_id_t>::iterator> lru_map_;
    std::mutex mutex_;
};

} // namespace forgedb
