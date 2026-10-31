#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <io/pixel_layout.hpp>
#include <render/gl/texture_format.hpp>
#include <view/display_params.hpp>

namespace enim {

/// @brief Converts native rows into display texture rows on the CPU: channel selection, depth conversion, range.
/// Output rows are tightly packed in the texture's layout (TextureFormat::channels samples of TextureFormat::sample).
/// Thread-safe: convert_rows() only reads the converter's state.
struct PixelConverter {
    /// @param premultiply Multiply the color channels by alpha (the texture's last channel, when it comes from the source).
    PixelConverter(const PixelLayout& src, std::size_t width, const TextureFormat& dst, const DisplayParams& display,
                   bool premultiply = false);

    std::size_t dst_row_bytes() const { return this->width * this->dst.bytes_per_pixel(); }

    /// @brief Converts count rows. dst_stride may be negative to write the rows in reverse order (bottom-up sources).
    void convert_rows(const std::uint8_t* src, std::size_t src_stride, std::size_t count, std::uint8_t* dst,
                      std::ptrdiff_t dst_stride) const;

    /// @brief Converts count consecutive pixels (any part of a row, e.g. one tile's segment).
    void convert_span(const std::uint8_t* src, std::size_t count, std::uint8_t* dst) const;

  private:
    PixelLayout src;
    std::size_t width;
    TextureFormat dst;
    TexelChannels texel;
    Vec2f range;
    bool swap_bytes;
    bool fast_u8; // U8 -> U8 with the default range: a channel shuffle
    int alpha = -1; // texture channel the others are multiplied by, -1 = straight alpha (or none)

    void convert_span_u8(const std::uint8_t* src, std::size_t count, std::uint8_t* dst) const;
    void convert_span_any(const std::uint8_t* src, std::size_t count, std::uint8_t* dst) const;
};

} // namespace enim
