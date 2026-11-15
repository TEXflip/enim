#pragma once

#include <mio.hpp>

#include <io/image_source.hpp>

namespace enim {

/// @brief WebP, decoded on the CPU by libwebp: lossy (VP8, with or without an ALPH chunk) and lossless (VP8L).
/// Decoded as RGB8, or RGBA8 (straight alpha) when the image has transparency. Animated files show their first
/// frame, placed on the canvas (transparent around it). ICC profiles, EXIF and XMP are ignored.
struct WebpSource : public ImageSource {
    std::expected<void, enim::Error> open(const std::filesystem::path& path) override;
    const ImageInfo& info() const override { return this->image_info; }
    FormatCaps caps() const override { return {.raw_mappable = false, .gpu_decodable = false, .streamable = true}; }
    /// @brief The RIFF header and every chunk before the image data, plus the header of the bitstream (dimensions).
    std::span<const std::uint8_t> header_bytes() const override;
    /// @brief Feeds the file to libwebp's incremental decoder piece by piece and emits the rows as they are decoded.
    /// The decoder writes the whole frame into a buffer the size of the image.
    std::expected<void, enim::Error> decode(RowSink& sink, std::stop_token stop) override;

  private:
    mio::mmap_source map;
    ImageInfo image_info;
    std::size_t header_size = 0;
    // the frame decoded: the whole image, or the first frame of an animation
    std::size_t bitstream_offset = 0, bitstream_size = 0;
    std::size_t frame_x = 0, frame_y = 0, frame_width = 0, frame_height = 0;
};

} // namespace enim
