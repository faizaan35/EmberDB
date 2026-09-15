#include "forgedb/storage/disk/disk_manager.h"
#include <filesystem>
#include <vector>

namespace forgedb {

DiskManager::DiskManager(std::string db_file) : db_file_(std::move(db_file)) {}

DiskManager::~DiskManager() {
    Close();
}

Status DiskManager::Open() {
    std::lock_guard<std::mutex> lock(db_io_latch_);
    if (is_open_) {
        return Status::OK();
    }

    // Ensure parent directory exists
    std::filesystem::path p(db_file_);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path());
    }

    // Open file in read/write binary mode
    db_io_.open(db_file_, std::ios::binary | std::ios::in | std::ios::out);
    if (!db_io_.is_open()) {
        // Clear error state and try creating the file
        db_io_.clear();
        db_io_.open(db_file_, std::ios::binary | std::ios::trunc | std::ios::out | std::ios::in);
        if (!db_io_.is_open()) {
            return Status::IOError("Failed to open or create database file: " + db_file_);
        }
    }

    // Determine current number of pages from file size
    db_io_.seekg(0, std::ios::end);
    std::streampos file_size = db_io_.tellg();
    if (file_size < 0) {
        file_size = 0;
    }
    num_pages_ = static_cast<size_t>(file_size) / PAGE_SIZE;

    is_open_ = true;
    return Status::OK();
}

Status DiskManager::Close() {
    if (flush_hook_) {
        flush_hook_();
    }
    std::lock_guard<std::mutex> lock(db_io_latch_);
    if (!is_open_) {
        return Status::OK();
    }

    db_io_.flush();
    db_io_.close();
    is_open_ = false;
    return Status::OK();
}

Result<page_id_t> DiskManager::AllocatePage() {
    std::lock_guard<std::mutex> lock(db_io_latch_);
    if (!is_open_) {
        return Status::IOError("Database file is not open");
    }

    page_id_t new_page_id = static_cast<page_id_t>(num_pages_);

    // Seek to the end of the file and write a blank 4096-byte page
    std::vector<char> blank(PAGE_SIZE, 0);
    db_io_.seekp(static_cast<std::streamoff>(new_page_id) * PAGE_SIZE);
    db_io_.write(blank.data(), PAGE_SIZE);
    if (!db_io_.good()) {
        db_io_.clear();
        return Status::IOError("Failed to allocate new page on disk");
    }

    db_io_.flush();
    ++num_pages_;
    return new_page_id;
}

Status DiskManager::ReadPage(page_id_t page_id, char* page_data) {
    std::lock_guard<std::mutex> lock(db_io_latch_);
    if (!is_open_) {
        return Status::IOError("Database file is not open");
    }

    if (page_id < 0 || static_cast<size_t>(page_id) >= num_pages_) {
        return Status::InvalidArgument("Page ID " + std::to_string(page_id) + " is out of bounds (total pages: " + std::to_string(num_pages_) + ")");
    }

    db_io_.seekg(static_cast<std::streamoff>(page_id) * PAGE_SIZE);
    db_io_.read(page_data, PAGE_SIZE);

    std::streamsize bytes_read = db_io_.gcount();
    if (bytes_read != static_cast<std::streamsize>(PAGE_SIZE)) {
        db_io_.clear();
        return Status::IOError("Incomplete page read: read " + std::to_string(bytes_read) + " bytes, expected " + std::to_string(PAGE_SIZE));
    }

    return Status::OK();
}

Status DiskManager::WritePage(page_id_t page_id, const char* page_data) {
    std::lock_guard<std::mutex> lock(db_io_latch_);
    if (!is_open_) {
        return Status::IOError("Database file is not open");
    }

    if (page_id < 0 || static_cast<size_t>(page_id) >= num_pages_) {
        return Status::InvalidArgument("Page ID " + std::to_string(page_id) + " is out of bounds (total pages: " + std::to_string(num_pages_) + ")");
    }

    db_io_.seekp(static_cast<std::streamoff>(page_id) * PAGE_SIZE);
    db_io_.write(page_data, PAGE_SIZE);
    if (!db_io_.good()) {
        db_io_.clear();
        return Status::IOError("Failed to write page " + std::to_string(page_id) + " to disk");
    }

    return Status::OK();
}

Status DiskManager::Flush() {
    if (flush_hook_) {
        flush_hook_();
    }
    std::lock_guard<std::mutex> lock(db_io_latch_);
    if (!is_open_) {
        return Status::IOError("Database file is not open");
    }

    db_io_.flush();
    if (!db_io_.good()) {
        db_io_.clear();
        return Status::IOError("Failed to flush database file");
    }
    return Status::OK();
}

size_t DiskManager::GetNumPages() const {
    std::lock_guard<std::mutex> lock(db_io_latch_);
    return num_pages_;
}

} // namespace forgedb
