#pragma once

#include "emberdb/common/config.h"
#include "emberdb/common/status.h"
#include "emberdb/recovery/log_record.h"
#include <atomic>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

namespace emberdb {

/**
 * LogManager coordinates write-ahead logging (WAL).
 * Manages an in-memory append buffer, assigns monotonic LSNs,
 * enforces synchronous or on-demand log flushing, and reads log records for crash recovery.
 */
class LogManager {
public:
    static constexpr size_t LOG_BUFFER_SIZE = 64 * 1024; // 64 KB

    explicit LogManager(std::string log_file_path);
    ~LogManager();

    Status Open();
    Status Close();
    bool IsOpen() const;

    // Append record to log buffer and assign LSN
    lsn_t AppendRecord(LogRecord& record);

    // Flush active log buffer to disk
    void FlushLogBuffer();

    // Ensure all log records up to and including `lsn` are flushed to disk
    void FlushLogBufferUpTo(lsn_t lsn);

    // LSN inspection
    lsn_t GetFlushedLSN() const { return flushed_lsn_.load(); }
    lsn_t GetLastLSN() const { return last_appended_lsn_.load(); }
    lsn_t GetNextLSN() const { return next_lsn_.load(); }

    // Read all records from the log file sequentially
    std::vector<LogRecord> ReadAllRecords();

    // Reset log file
    Status Truncate();

    const std::string& GetLogFilePath() const { return log_file_path_; }

private:
    std::string log_file_path_;
    std::fstream log_file_;
    bool is_open_{false};

    char log_buffer_[LOG_BUFFER_SIZE];
    size_t log_buffer_offset_{0};

    std::atomic<lsn_t> next_lsn_{1};
    std::atomic<lsn_t> last_appended_lsn_{INVALID_LSN};
    std::atomic<lsn_t> flushed_lsn_{INVALID_LSN};

    mutable std::mutex latch_;
};

} // namespace emberdb
