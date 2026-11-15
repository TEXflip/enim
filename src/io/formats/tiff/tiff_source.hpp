#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <mio.hpp>

#include <io/formats/tiff/tiff_convert.hpp>
#include <io/image_source.hpp>

namespace enim {

/// @brief TIFF and BigTIFF, read by libtiff from the mapped file. Every compression libtiff supports except JBIG,
/// strips and tiles, chunky and planar data, any bit depth and sample format.
/// - Uncompressed images stored contiguously, in a layout GL or the convert pass can read, are raw-mappable.
/// - Others are decoded in bands of strips or tile rows (in parallel, one libtiff handle per worker) and converted
///   to a native layout: odd bit depths are widened, palette and CMYK become RGB, MinIsWhite is inverted,
///   associated alpha is un-premultiplied, LogLuv becomes float linear RGB, 64-bit samples become F32.
/// - What libtiff can only read through TIFFRGBAImage (YCbCr without JPEG, CIE Lab, old JPEG, ...) becomes RGBA8.
/// The first full-resolution page is shown. Orientations 1-4 are applied, 5-8 (transposed) are shown as stored.
struct TiffSource : public ImageSource {
    TiffSource();
    ~TiffSource() override;

    std::expected<void, enim::Error> open(const std::filesystem::path& path) override;
    const ImageInfo& info() const override { return this->image_info; }
    FormatCaps caps() const override { return {.raw_mappable = this->raw, .gpu_decodable = false, .streamable = this->streamable}; }
    /// @brief The file header and the raw bytes of the image's IFD: size, layout, compression, strip/tile offsets.
    std::span<const std::uint8_t> header_bytes() const override { return this->header; }
    std::span<const std::uint8_t> mapped_pixels() override;
    std::expected<void, enim::Error> decode(RowSink& sink, std::stop_token stop) override;

    enum class Path : std::uint8_t {
        Raw,      // rows straight from the mmap
        Native,   // libtiff's decoded rows are already in the output layout
        Convert,  // decoded rows go through tiff::convert_row
        Rgba,     // TIFFRGBAImage, RGBA8
    };

  private:
    mio::mmap_source map;
    std::string path_name; // for libtiff's messages
    ImageInfo image_info;
    std::vector<std::uint8_t> header;
    std::uint32_t directory = 0;   // the IFD shown
    std::uint16_t orientation = 1; // TIFF Orientation tag

    Path path = Path::Native;
    tiff::ConvertSpec spec;      // describes the decoded rows (and their conversion for Path::Convert)
    bool raw = false;
    bool streamable = true;
    std::size_t raw_offset = 0;  // Raw: file offset of the first row

    bool tiled = false;
    std::uint32_t unit_width = 0;  // tile width, or the image width for strips
    std::uint32_t unit_rows = 0;   // tile height or rows per strip
    bool scanlines = false;        // huge strips: read row by row with TIFFReadScanline
    bool rgba_alpha = false;       // Rgba: the image has an alpha channel
    bool jpeg_rgb = false;         // YCbCr JPEG: decoded to RGB by libjpeg
    bool sgilog_float = false;     // LogLuv/LogL: decoded to float

    std::expected<void, enim::Error> decode_rgba(RowSink& sink, std::stop_token stop);
    std::expected<void, enim::Error> decode_scanlines(RowSink& sink, std::stop_token stop);
    std::expected<void, enim::Error> decode_units(RowSink& sink, std::stop_token stop);
};

} // namespace enim
