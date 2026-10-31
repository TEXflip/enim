#pragma once

#include <mio.hpp>

#include <io/image_source.hpp>

namespace enim {

/// @brief JPEG (baseline, progressive, arithmetic, lossless), 8-bit gray or color, decoded on the CPU by libjpeg-turbo.
/// Color images are converted to RGB by libjpeg-turbo. CMYK/YCCK and 12/16-bit images are not supported yet.
struct JpegSource : public ImageSource {
    std::expected<void, enim::Error> open(const std::filesystem::path& path) override;
    const ImageInfo& info() const override { return this->image_info; }
    FormatCaps caps() const override { return {.raw_mappable = false, .gpu_decodable = false, .streamable = true}; }
    /// @brief From SOI to the end of the first SOS segment: dimensions, quantization and Huffman tables.
    std::span<const std::uint8_t> header_bytes() const override;
    /// @brief Emits bands of rows top to bottom. Progressive files are read whole first, then emitted the same way.
    std::expected<void, enim::Error> decode(RowSink& sink, std::stop_token stop) override;

  private:
    mio::mmap_source map; // read-only mapping, read by libjpeg-turbo through jpeg_mem_src
    ImageInfo image_info;
    std::size_t header_size = 0;
};

} // namespace enim
