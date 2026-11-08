#pragma once

#include <concepts>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <expected>
#include <mutex>
#include <optional>
#include <stop_token>
#include <vector>

#include <errors.hpp>

namespace enim {

/// @brief Fast mode: full-width texture rows converted to the texture's layout, waiting to be uploaded.
struct UploadBand {
    std::size_t first_row = 0; // texture row (top-down)
    std::size_t rows = 0;
    std::vector<std::uint8_t> pixels; // tightly packed rows
};

/// @brief Anything the UploadQueue can carry: its size is its pixel buffer.
template <typename T>
concept UploadItem = requires(const T& item) {
    { item.pixels.size() } -> std::convertible_to<std::size_t>;
};

/// @brief Hands converted pixels (fast mode bands, normal mode tiles) from worker threads to the main thread (the only
/// one making GL calls).
/// Bounded: push() waits while the queued bytes exceed the capacity, so a fast decoder can't fill the RAM.
template <UploadItem Item>
struct UploadQueue {
    explicit UploadQueue(std::size_t capacity_bytes) : capacity_bytes(capacity_bytes) {}

    /// @brief Queues an item, waiting for room. An item larger than the capacity is accepted when the queue is empty.
    /// @return False if stop was requested while waiting.
    bool push(Item item, std::stop_token stop) {
        std::unique_lock lock(this->mutex);
        bool room = this->room_cv.wait(lock, stop, [&] {
            return this->queued_bytes == 0 || this->queued_bytes + item.pixels.size() <= this->capacity_bytes;
        });
        if (!room)
            return false;
        this->queued_bytes += item.pixels.size();
        this->items.push_back(std::move(item));
        return true;
    }

    /// @brief Takes the oldest item, if any. Main thread.
    std::optional<Item> pop() {
        std::lock_guard lock(this->mutex);
        if (this->items.empty())
            return std::nullopt;
        Item item = std::move(this->items.front());
        this->items.pop_front();
        this->queued_bytes -= item.pixels.size();
        this->room_cv.notify_all();
        return item;
    }

    /// @brief Marks the end of the stream, with the producer's result.
    void finish(std::expected<void, enim::Error> result) {
        std::lock_guard lock(this->mutex);
        this->result = std::move(result);
    }

    /// @brief True if there are items to upload, or the stream ended and its result wasn't taken yet.
    bool pending() const {
        std::lock_guard lock(this->mutex);
        return !this->items.empty() || (this->result && !this->result_taken);
    }

    /// @brief The producer's result once the stream ended and every item was popped; nullopt before. Main thread.
    std::optional<std::expected<void, enim::Error>> take_result() {
        std::lock_guard lock(this->mutex);
        if (!this->items.empty() || !this->result || this->result_taken)
            return std::nullopt;
        this->result_taken = true;
        return this->result;
    }

  private:
    mutable std::mutex mutex;
    std::condition_variable_any room_cv;
    std::deque<Item> items;
    std::size_t queued_bytes = 0;
    std::size_t capacity_bytes;
    std::optional<std::expected<void, enim::Error>> result;
    bool result_taken = false;
};

using BandQueue = UploadQueue<UploadBand>;

} // namespace enim
