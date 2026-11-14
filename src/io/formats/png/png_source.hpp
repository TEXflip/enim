#pragma once

#include <mio.hpp>

#include <io/image_source.hpp>

namespace enim {

/// @brief PNG, decoded on the CPU by libspng (over zlib-ng). Every color type and bit depth.
/// - 8 and 16-bit gray, gray+alpha, RGB and RGBA keep their layout (16-bit samples in host byte order).
/// - Palette images become RGB8 (RGBA8 with tRNS), 1/2/4-bit gray becomes 8-bit gray.
/// - A tRNS chunk on gray or RGB images adds an alpha channel.
/// Gamma, color profiles and sBIT are ignored. Checksums (chunk CRCs, Adler-32) are not verified, for speed.
struct PngSource : public ImageSource {
    std::expected<void, enim::Error> open(const std::filesystem::path& path) override;
    const ImageInfo& info() const override { return this->image_info; }
    /// @brief Interlaced (Adam7) images are not streamable: no row is final before the 6th of the 7 passes.
    FormatCaps caps() const override { return {.raw_mappable = false, .gpu_decodable = false, .streamable = !this->interlaced}; }
    /// @brief From the signature to the header of the first IDAT chunk: IHDR, PLTE, tRNS and the other chunks before it.
    std::span<const std::uint8_t> header_bytes() const override;
    /// @brief Emits bands of rows top to bottom. Interlaced images are decoded whole into a temporary buffer first,
    /// then emitted the same way.
    std::expected<void, enim::Error> decode(RowSink& sink, std::stop_token stop) override;

  private:
    mio::mmap_source map; // read-only mapping, read by libspng through spng_set_png_buffer
    ImageInfo image_info;
    std::size_t header_size = 0;
    int format = 0;       // spng_format libspng decodes to
    int decode_flags = 0; // spng_decode_flags (SPNG_DECODE_TRNS)
    bool interlaced = false;
};

} // namespace enim
