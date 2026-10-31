#pragma once

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

/// @brief Rows converted to the texture's layout, waiting for the main thread to upload them.
struct UploadBand {
    std::size_t first_row = 0; // texture row (top-down)
    std::size_t rows = 0;
    std::vector<std::uint8_t> pixels; // tightly packed rows
    // normal mode frames
    std::uint64_t generation = 0; // the FrameRequest the band belongs to
    bool end_of_frame = false;    // no pixels: every band of the frame was queued
    bool complete = true;         // with end_of_frame: no fallback was used, the frame doesn't need refining
};

/// @brief Hands converted bands (texture rows in fast mode, frame bands in normal mode) from worker threads to the main
/// thread (the only one making GL calls).
/// Bounded: push() waits while the queued bytes exceed the capacity, so a fast decoder can't fill the RAM.
struct UploadQueue {
    explicit UploadQueue(std::size_t capacity_bytes) : capacity_bytes(capacity_bytes) {}

    /// @brief Queues a band, waiting for room. A band larger than the capacity is accepted when the queue is empty.
    /// @return False if stop was requested while waiting.
    bool push(UploadBand band, std::stop_token stop) {
        std::unique_lock lock(this->mutex);
        bool room = this->room_cv.wait(lock, stop, [&] {
            return this->queued_bytes == 0 || this->queued_bytes + band.pixels.size() <= this->capacity_bytes;
        });
        if (!room)
            return false;
        this->queued_bytes += band.pixels.size();
        this->bands.push_back(std::move(band));
        return true;
    }

    /// @brief Takes the oldest band, if any. Main thread.
    std::optional<UploadBand> pop() {
        std::lock_guard lock(this->mutex);
        if (this->bands.empty())
            return std::nullopt;
        UploadBand band = std::move(this->bands.front());
        this->bands.pop_front();
        this->queued_bytes -= band.pixels.size();
        this->room_cv.notify_all();
        return band;
    }

    /// @brief Marks the end of the stream, with the producer's result.
    void finish(std::expected<void, enim::Error> result) {
        std::lock_guard lock(this->mutex);
        this->result = std::move(result);
    }

    /// @brief True if there are bands to upload, or the stream ended and its result wasn't taken yet.
    bool pending() const {
        std::lock_guard lock(this->mutex);
        return !this->bands.empty() || (this->result && !this->result_taken);
    }

    /// @brief The producer's result once the stream ended and every band was popped; nullopt before. Main thread.
    std::optional<std::expected<void, enim::Error>> take_result() {
        std::lock_guard lock(this->mutex);
        if (!this->bands.empty() || !this->result || this->result_taken)
            return std::nullopt;
        this->result_taken = true;
        return this->result;
    }

  private:
    mutable std::mutex mutex;
    std::condition_variable_any room_cv;
    std::deque<UploadBand> bands;
    std::size_t queued_bytes = 0;
    std::size_t capacity_bytes;
    std::optional<std::expected<void, enim::Error>> result;
    bool result_taken = false;
};

} // namespace enim
