#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>

#include <errors.hpp>

namespace enim {

/// @brief Receives decoded rows in the source's native PixelLayout.
/// Rows arrive in storage order: y0 is the storage row index (see PixelLayout::rows).
struct RowSink {
    virtual ~RowSink() = default;
    virtual void on_rows(std::size_t y0, std::size_t count, const std::uint8_t* data, std::size_t stride) = 0;
    virtual void on_done(std::expected<void, enim::Error> result) = 0;

    /// @brief Optional: a buffer for the full image the decoder can write into directly. Empty if unsupported.
    virtual std::span<std::uint8_t> full_image_buffer() { return {}; }
};

} // namespace enim
