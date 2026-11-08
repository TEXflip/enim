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
    /// @param src How the source pixels are stored: sample type, channel count (the pixel step), and byte order
    ///            (big_endian selects byte swapping for multi-byte samples). Its stride and row order are not used:
    ///            the stride is passed to convert_rows() and the row order is handled with a negative dst_stride.
    /// @param width Pixels per row: the number of pixels convert_rows() converts per row, and the width used by
    ///            dst_row_bytes(). convert_span() ignores it and takes its own count.
    /// @param dst The texture format to write: its channel count and sample type define the packed output layout
    ///            (U8/U16 are clamped to [0, 1] and quantized, F16/F32 are written as floats, F32 unclamped for HDR).
    ///            Its swizzle is not applied here.
    /// @param display What to show: channel_map/constant pick, for each texture channel, a source channel or a constant
    ///            (resolved by texel_channels()), and range is the source value interval mapped to [0, 1].
    ///            U8 -> U8 with the default range takes a fast path that only shuffles bytes.
    /// @param premultiply Multiply the color channels by alpha (the texture's last channel). Only applies when dst has
    ///            2 or 4 channels and that channel comes from the source: a constant alpha is 1 and changes nothing.
    PixelConverter(const PixelLayout& src, std::size_t width, const TextureFormat& dst, const DisplayParams& display, bool premultiply = false);

    std::size_t dst_row_bytes() const { return this->width * this->dst.bytes_per_pixel(); }

    /// @brief Converts count rows. dst_stride may be negative to write the rows in reverse order (bottom-up sources).
    void convert_rows(const std::uint8_t* src, std::size_t src_stride, std::size_t count, std::uint8_t* dst, std::ptrdiff_t dst_stride) const;

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
