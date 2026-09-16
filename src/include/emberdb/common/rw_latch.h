#pragma once

#include <mutex>
#include <condition_variable>
#include <shared_mutex>

namespace emberdb {

/**
 * Cross-platform reader-writer latch implemented using pure C++17 primitives.
 * Avoids platform-specific macro collisions (<windows.h>) and MinGW winpthreads rwlock defects.
 * Provides writer-preference scheduling to avoid writer starvation.
 * Compatible with std::unique_lock and std::shared_lock.
 */
class ReaderWriterLatch {
public:
    ReaderWriterLatch() = default;
    ~ReaderWriterLatch() = default;

    ReaderWriterLatch(const ReaderWriterLatch&) = delete;
    ReaderWriterLatch& operator=(const ReaderWriterLatch&) = delete;

    void lock() {
        std::unique_lock<std::mutex> lk(mutex_);
        ++waiting_writers_;
        cv_.wait(lk, [this]() { return !writer_active_ && readers_count_ == 0; });
        --waiting_writers_;
        writer_active_ = true;
    }

    void unlock() {
        {
            std::lock_guard<std::mutex> lk(mutex_);
            writer_active_ = false;
        }
        cv_.notify_all();
    }

    void lock_shared() {
        std::unique_lock<std::mutex> lk(mutex_);
        cv_.wait(lk, [this]() { return !writer_active_ && waiting_writers_ == 0; });
        ++readers_count_;
    }

    void unlock_shared() {
        {
            std::lock_guard<std::mutex> lk(mutex_);
            --readers_count_;
        }
        cv_.notify_all();
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    int readers_count_{0};
    int waiting_writers_{0};
    bool writer_active_{false};
};

} // namespace emberdb
