#pragma once

#include "forgedb/common/config.h"
#include "forgedb/common/status.h"
#include <string>
#include <fstream>
#include <mutex>
#include <functional>

namespace forgedb {

/**
 * DiskManager is responsible for reading and writing raw 4096-byte pages to disk files.
 * Provides block-level page allocation, buffered flushing, and persistence tracking.
 */
class DiskManager {
public:
    explicit DiskManager(std::string db_file);
    ~DiskManager();

    // Disable copy
    DiskManager(const DiskManager&) = delete;
    DiskManager& operator=(const DiskManager&) = delete;

    Status Open();
    Status Close();
    bool IsOpen() const { return is_open_; }

    Result<page_id_t> AllocatePage();
    Status ReadPage(page_id_t page_id, char* page_data);
    Status WritePage(page_id_t page_id, const char* page_data);
    Status Flush();

    size_t GetNumPages() const;
    const std::string& GetFileName() const { return db_file_; }

    void SetFlushHook(std::function<void()> hook) { flush_hook_ = std::move(hook); }

private:
    std::string db_file_;
    std::fstream db_io_;
    bool is_open_{false};
    mutable std::mutex db_io_latch_;
    size_t num_pages_{0};
    std::function<void()> flush_hook_;
};

} // namespace forgedb
