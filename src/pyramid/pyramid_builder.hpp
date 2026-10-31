#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include <io/pixel_layout.hpp>
#include <io/row_sink.hpp>
#include <pyramid/pyramid_store.hpp>

namespace enim {

/// @brief Normal mode: writes the decoded rows into level 0 of the pyramid and cascades 2x box downsampling into levels
/// 1..N while level 0 is still being produced, in the native sample type. Works for any row order: rows arrive in
/// storage order and each level pairs row y with row y^1 (top-down), whichever comes first.
struct PyramidBuilderSink : public RowSink {
    /// @param wakeup Called on the decode thread at most every ~100 ms of progress, and when done.
    PyramidBuilderSink(PyramidStore& store, const ImageInfo& info, std::function<void()> wakeup);

    void on_rows(std::size_t y0, std::size_t count, const std::uint8_t* data, std::size_t stride) override;
    void on_done(std::expected<void, enim::Error> result) override;

    /// @brief True once on_done() ran: the decode thread is about to return. Any thread.
    bool finished() const { return this->finished_flag.load(std::memory_order_acquire); }

    /// @brief The decoder's result once on_done() ran (decode thread; read it after joining).
    const std::optional<std::expected<void, enim::Error>>& result() const { return this->done; }

  private:
    struct PendingRow {
        std::vector<std::uint8_t> pixels; // width px of the level, tightly packed
        std::size_t y = 0;
        bool valid = false;
    };

    PyramidStore& store;
    ImageInfo info;
    std::function<void()> wakeup;
    std::size_t bpp;
    bool swap;
    std::vector<PendingRow> pending;              // per level: a row waiting for its partner
    std::vector<std::vector<std::uint8_t>> scratch; // per level: the downsampled output row
    std::chrono::steady_clock::time_point last_wake;
    std::optional<std::expected<void, enim::Error>> done;
    std::atomic<bool> finished_flag{false};

    /// @brief Stores row y of a level, then pairs it with its partner to produce row y/2 of the next level.
    void add_row(int level, std::size_t y, const std::uint8_t* row);
    /// @brief Averages rows a and b (b may be null: a single row) of a level into one row of the next level.
    void downsample(int level, const std::uint8_t* a, const std::uint8_t* b, std::uint8_t* out) const;
};

} // namespace enim
