// Fixed-capacity multi-producer/multi-consumer queue. When full, push()
// drops the oldest item and counts it: for a live feed, fresh data matters
// more than old data, and the reader thread must never block on a slow
// consumer (the TCP peer would stall or disconnect us).
#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>

namespace maritime::ingest {

template <class T>
class BoundedQueue {
public:
    explicit BoundedQueue(std::size_t capacity) : capacity_(capacity == 0 ? 1 : capacity) {}

    // Returns false if the queue is closed (the item is not added).
    bool push(T item) {
        {
            const std::lock_guard lock(mu_);
            if (closed_) return false;
            if (items_.size() == capacity_) {
                items_.pop_front();
                ++dropped_;
            }
            items_.push_back(std::move(item));
            ++pushed_;
        }
        cv_.notify_one();
        return true;
    }

    // Blocks until an item is available, the queue is closed and empty, or
    // the timeout expires. Returns nullopt in the last two cases.
    template <class Rep, class Period>
    std::optional<T> pop(std::chrono::duration<Rep, Period> timeout) {
        std::unique_lock lock(mu_);
        cv_.wait_for(lock, timeout, [&] { return !items_.empty() || closed_; });
        if (items_.empty()) return std::nullopt;
        T item = std::move(items_.front());
        items_.pop_front();
        return item;
    }

    // Wakes all waiters; remaining items can still be popped.
    void close() {
        {
            const std::lock_guard lock(mu_);
            closed_ = true;
        }
        cv_.notify_all();
    }

    [[nodiscard]] bool closed_and_empty() const {
        const std::lock_guard lock(mu_);
        return closed_ && items_.empty();
    }
    [[nodiscard]] std::uint64_t dropped() const {
        const std::lock_guard lock(mu_);
        return dropped_;
    }
    [[nodiscard]] std::uint64_t pushed() const {
        const std::lock_guard lock(mu_);
        return pushed_;
    }
    [[nodiscard]] std::size_t size() const {
        const std::lock_guard lock(mu_);
        return items_.size();
    }

private:
    const std::size_t capacity_;
    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::deque<T> items_;
    bool closed_ = false;
    std::uint64_t dropped_ = 0;
    std::uint64_t pushed_ = 0;
};

}  // namespace maritime::ingest
