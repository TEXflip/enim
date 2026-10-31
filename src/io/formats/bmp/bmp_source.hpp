#pragma once

#include <array>
#include <cstdint>

#include <mio.hpp>

#include <io/image_source.hpp>

namespace enim {

/// @brief Uncompressed 24 and 32 bit BMP (BI_RGB, BI_BITFIELDS with byte-aligned masks).
struct BmpSource : public ImageSource {

#pragma pack(push, 1)
    struct BmpHeader {
        std::array<char, 2> signature;
        std::uint32_t file_size;
        std::uint32_t reserved;
        std::uint32_t file_offset_to_pixel_array;

        // DIB header V3 fields
        std::uint32_t dib_header_size;
        std::int32_t width;
        std::int32_t height; // negative for top-down images
        std::uint16_t planes;
        std::uint16_t bits_per_pixel;
        std::uint32_t compression_method;
        std::uint32_t image_size;
        std::int32_t W_px_per_meter;
        std::int32_t H_px_per_meter;
        std::uint32_t colors_in_palette;
        std::uint32_t important_colors;
    };
#pragma pack(pop)

    static_assert(sizeof(BmpHeader) == 54);

    std::expected<void, enim::Error> open(const std::filesystem::path& path) override;
    const ImageInfo& info() const override { return this->image_info; }
    FormatCaps caps() const override { return {.raw_mappable = true, .gpu_decodable = false, .streamable = true}; }
    std::span<const std::uint8_t> header_bytes() const override;
    std::span<const std::uint8_t> mapped_pixels() override;
    std::expected<void, enim::Error> decode(RowSink& sink, std::stop_token stop) override;

  private:
    mio::mmap_source map; // read-only mapping
    BmpHeader header{};
    ImageInfo image_info;

    std::expected<void, enim::Error> read_channel_roles();
};

} // namespace enim
