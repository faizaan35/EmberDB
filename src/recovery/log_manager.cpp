#include "emberdb/recovery/log_manager.h"
#include <cstring>
#include <filesystem>
#include <iostream>

namespace emberdb {

LogManager::LogManager(std::string log_file_path)
    : log_file_path_(std::move(log_file_path)) {}

LogManager::~LogManager() {
    Close();
}

Status LogManager::Open() {
    std::lock_guard<std::mutex> lock(latch_);
    if (is_open_) {
        return Status::OK();
    }

    // Ensure parent directory exists
    std::filesystem::path p(log_file_path_);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path());
    }

    // Ensure file exists by opening in append mode
    {
        std::ofstream touch(log_file_path_, std::ios::app | std::ios::binary);
    }

    // Open file in read-write binary mode
    log_file_.open(log_file_path_, std::ios::in | std::ios::out | std::ios::binary);
    if (!log_file_.is_open()) {
        return Status::IOError("Failed to open WAL file: " + log_file_path_);
    }

    is_open_ = true;
    log_buffer_offset_ = 0;

    // Scan existing log records to initialize LSN sequence
    log_file_.seekg(0, std::ios::beg);
    lsn_t max_lsn = INVALID_LSN;

    while (log_file_.peek() != EOF) {
        uint32_t rec_size = 0;
        log_file_.read(reinterpret_cast<char*>(&rec_size), sizeof(uint32_t));
        if (log_file_.gcount() < static_cast<std::streamsize>(sizeof(uint32_t))) {
            break;
        }
        if (rec_size < sizeof(uint32_t)) {
            break;
        }
        std::vector<char> buf(rec_size);
        std::memcpy(buf.data(), &rec_size, sizeof(uint32_t));
        log_file_.read(buf.data() + sizeof(uint32_t), rec_size - sizeof(uint32_t));
        if (log_file_.gcount() < static_cast<std::streamsize>(rec_size - sizeof(uint32_t))) {
            break;
        }
        LogRecord rec;
        if (LogRecord::Deserialize(buf.data(), rec_size, rec)) {
            if (rec.GetLSN() > max_lsn) {
                max_lsn = rec.GetLSN();
            }
        } else {
            break;
        }
    }

    // Clear EOF flags and seek write pointer to end
    log_file_.clear();
    log_file_.seekp(0, std::ios::end);

    if (max_lsn != INVALID_LSN) {
        next_lsn_.store(max_lsn + 1);
        last_appended_lsn_.store(max_lsn);
        flushed_lsn_.store(max_lsn);
    } else {
        next_lsn_.store(1);
        last_appended_lsn_.store(INVALID_LSN);
        flushed_lsn_.store(INVALID_LSN);
    }

    return Status::OK();
}

Status LogManager::Close() {
    std::lock_guard<std::mutex> lock(latch_);
    if (!is_open_) {
        return Status::OK();
    }

    // Flush any pending records in buffer
    if (log_buffer_offset_ > 0) {
        log_file_.write(log_buffer_, log_buffer_offset_);
        log_file_.flush();
        flushed_lsn_.store(last_appended_lsn_.load());
        log_buffer_offset_ = 0;
    }

    log_file_.close();
    is_open_ = false;
    return Status::OK();
}

bool LogManager::IsOpen() const {
    std::lock_guard<std::mutex> lock(latch_);
    return is_open_;
}

lsn_t LogManager::AppendRecord(LogRecord& record) {
    std::lock_guard<std::mutex> lock(latch_);
    if (!is_open_) {
        return INVALID_LSN;
    }

    uint32_t rec_size = record.CalculateSize();
    if (log_buffer_offset_ + rec_size > LOG_BUFFER_SIZE) {
        // Log buffer full: flush to disk first
        if (log_buffer_offset_ > 0) {
            log_file_.write(log_buffer_, log_buffer_offset_);
            log_file_.flush();
            flushed_lsn_.store(last_appended_lsn_.load());
            log_buffer_offset_ = 0;
        }
    }

    lsn_t lsn = next_lsn_++;
    record.SetLSN(lsn);
    record.Serialize(log_buffer_ + log_buffer_offset_);
    log_buffer_offset_ += rec_size;
    last_appended_lsn_.store(lsn);

    return lsn;
}

void LogManager::FlushLogBuffer() {
    std::lock_guard<std::mutex> lock(latch_);
    if (!is_open_ || log_buffer_offset_ == 0) {
        return;
    }

    log_file_.write(log_buffer_, log_buffer_offset_);
    log_file_.flush();
    flushed_lsn_.store(last_appended_lsn_.load());
    log_buffer_offset_ = 0;
}

void LogManager::FlushLogBufferUpTo(lsn_t lsn) {
    if (lsn == INVALID_LSN) {
        return;
    }

    std::lock_guard<std::mutex> lock(latch_);
    if (!is_open_) {
        return;
    }

    if (flushed_lsn_.load() < lsn && log_buffer_offset_ > 0) {
        log_file_.write(log_buffer_, log_buffer_offset_);
        log_file_.flush();
        flushed_lsn_.store(last_appended_lsn_.load());
        log_buffer_offset_ = 0;
    }
}

std::vector<LogRecord> LogManager::ReadAllRecords() {
    std::lock_guard<std::mutex> lock(latch_);

    // Flush active buffer to file so reader sees all committed writes
    if (is_open_ && log_buffer_offset_ > 0) {
        log_file_.write(log_buffer_, log_buffer_offset_);
        log_file_.flush();
        flushed_lsn_.store(last_appended_lsn_.load());
        log_buffer_offset_ = 0;
    }

    std::vector<LogRecord> records;
    std::ifstream file(log_file_path_, std::ios::binary);
    if (!file.is_open()) {
        return records;
    }

    while (file.peek() != EOF) {
        uint32_t rec_size = 0;
        file.read(reinterpret_cast<char*>(&rec_size), sizeof(uint32_t));
        if (file.gcount() < static_cast<std::streamsize>(sizeof(uint32_t))) {
            break;
        }
        if (rec_size < sizeof(uint32_t)) {
            break;
        }
        std::vector<char> buf(rec_size);
        std::memcpy(buf.data(), &rec_size, sizeof(uint32_t));
        file.read(buf.data() + sizeof(uint32_t), rec_size - sizeof(uint32_t));
        if (file.gcount() < static_cast<std::streamsize>(rec_size - sizeof(uint32_t))) {
            break;
        }
        LogRecord rec;
        if (LogRecord::Deserialize(buf.data(), rec_size, rec)) {
            records.push_back(std::move(rec));
        } else {
            break;
        }
    }

    return records;
}

Status LogManager::Truncate() {
    std::lock_guard<std::mutex> lock(latch_);
    if (is_open_) {
        log_file_.close();
    }

    std::ofstream trunc_file(log_file_path_, std::ios::trunc | std::ios::binary);
    trunc_file.close();

    log_buffer_offset_ = 0;
    next_lsn_.store(1);
    last_appended_lsn_.store(INVALID_LSN);
    flushed_lsn_.store(INVALID_LSN);

    if (is_open_) {
        log_file_.open(log_file_path_, std::ios::in | std::ios::out | std::ios::binary);
        if (!log_file_.is_open()) {
            is_open_ = false;
            return Status::IOError("Failed to reopen truncated WAL file: " + log_file_path_);
        }
    }

    return Status::OK();
}

} // namespace emberdb
