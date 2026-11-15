#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <utility>

namespace mp::core {

// Blocking multi-producer / multi-consumer queue with a size limit, for the scanner pipeline
// (walker -> tag readers -> database writer). The limit is what keeps memory flat: a fast
// producer simply waits when the consumers are behind. Never used on the audio thread.
template <typename T>
class BoundedQueue {
public:
    enum class PopResult { Item, Timeout, Closed };

    explicit BoundedQueue(std::size_t capacity) : capacity_(capacity == 0 ? 1 : capacity) {}

    // Waits while the queue is full. Returns false (and drops the value) once closed.
    bool push(T value) {
        std::unique_lock lock(mutex_);
        notFull_.wait(lock, [&] { return closed_ || items_.size() < capacity_; });
        if (closed_) {
            return false;
        }
        items_.push_back(std::move(value));
        notEmpty_.notify_one();
        return true;
    }

    // Waits for an item. Returns false when the queue is closed and empty.
    bool pop(T& out) {
        std::unique_lock lock(mutex_);
        notEmpty_.wait(lock, [&] { return closed_ || !items_.empty(); });
        return take(out);
    }

    // Like pop, but gives up after `timeout` (the writer uses this to commit when input goes quiet).
    PopResult popFor(T& out, std::chrono::milliseconds timeout) {
        std::unique_lock lock(mutex_);
        if (!notEmpty_.wait_for(lock, timeout, [&] { return closed_ || !items_.empty(); })) {
            return PopResult::Timeout;
        }
        return take(out) ? PopResult::Item : PopResult::Closed;
    }

    // No more pushes are accepted; consumers still receive what is already queued, then see "closed".
    void close() {
        {
            std::lock_guard lock(mutex_);
            closed_ = true;
        }
        notEmpty_.notify_all();
        notFull_.notify_all();
    }

private:
    bool take(T& out) {  // lock held
        if (items_.empty()) {
            return false;
        }
        out = std::move(items_.front());
        items_.pop_front();
        notFull_.notify_one();
        return true;
    }

    std::mutex mutex_;
    std::condition_variable notEmpty_;
    std::condition_variable notFull_;
    std::deque<T> items_;
    std::size_t capacity_;
    bool closed_ = false;
};

}  // namespace mp::core
