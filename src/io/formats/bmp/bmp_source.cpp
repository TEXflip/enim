#include "bmp_source.hpp"

#include <cstdlib>
#include <cstring>

namespace {

// compression methods (BI_RGB, BI_BITFIELDS, BI_ALPHABITFIELDS); the Windows macros may not be defined
constexpr std::uint32_t kBiRgb = 0;
constexpr std::uint32_t kBiBitfields = 3;
constexpr std::uint32_t kBiAlphaBitfields = 6;

constexpr std::size_t kFileHeaderSize = 14;
constexpr std::size_t kRowsPerChunk = 256;

} // namespace

namespace enim {

std::expected<void, enim::Error> BmpSource::open(const std::filesystem::path& path) {
    std::error_code ec;
    this->map.map(path.string(), ec);
    if (ec)
        return make_error(FileOpenError, ec.message());
    if (this->map.size() < sizeof(BmpHeader))
        return make_error(CorruptImageError, "BMP file too small");

    std::memcpy(&this->header, this->map.data(), sizeof(BmpHeader));

    if (header.signature[0] != 'B' || header.signature[1] != 'M')
        return make_error(CorruptImageError, "Invalid BMP signature");
    if (header.dib_header_size < 40)
        return make_error(UnsupportedFormatError, "Unsupported BMP header (OS/2 BITMAPCOREHEADER)");
    if (header.width <= 0 || header.height == 0)
        return make_error(CorruptImageError, "Invalid BMP dimensions");
    if (header.bits_per_pixel != 24 && header.bits_per_pixel != 32)
        return make_error(UnsupportedFormatError, "Only 24-bit and 32-bit BMPs are supported");

    std::size_t width = static_cast<std::size_t>(header.width);
    std::size_t height = static_cast<std::size_t>(std::abs(static_cast<std::int64_t>(header.height)));

    PixelLayout& layout = this->image_info.layout;
    layout.sample = SampleType::U8;
    layout.channels = static_cast<std::uint8_t>(header.bits_per_pixel / 8);
    layout.stride = ((width * header.bits_per_pixel + 31) / 32) * 4; // rows are padded to 4 bytes
    layout.rows = header.height < 0 ? RowOrder::TopDown : RowOrder::BottomUp;
    layout.big_endian = false;

    auto roles_result = this->read_channel_roles();
    if (!roles_result)
        return roles_result;

    this->image_info.width = width;
    this->image_info.height = height;

    if (header.file_offset_to_pixel_array > this->map.size() ||
        this->map.size() - header.file_offset_to_pixel_array < layout.stride * height)
        return make_error(CorruptImageError, "BMP pixel data is truncated");

    return {};
}

std::expected<void, enim::Error> BmpSource::read_channel_roles() {
    PixelLayout& layout = this->image_info.layout;

    if (header.compression_method == kBiRgb) {
        // little-endian BGR(X): the 4th byte of a 32-bit BI_RGB pixel is unused
        layout.roles[0] = ChannelRole::B;
        layout.roles[1] = ChannelRole::G;
        layout.roles[2] = ChannelRole::R;
        layout.roles[3] = ChannelRole::Unused;
        return {};
    }

    if ((header.compression_method != kBiBitfields && header.compression_method != kBiAlphaBitfields) || header.bits_per_pixel != 32)
        return make_error(UnsupportedFormatError, "Compressed BMPs are not supported");

    // masks are inside V2+ headers, or follow a 40-byte header
    std::size_t mask_count = header.compression_method == kBiAlphaBitfields || header.dib_header_size >= 56 ? 4 : 3;
    std::size_t masks_offset = kFileHeaderSize + 40;
    if (this->map.size() < masks_offset + mask_count * 4)
        return make_error(CorruptImageError, "BMP bitfield masks are truncated");

    std::array<std::uint32_t, 4> masks{};
    std::memcpy(masks.data(), this->map.data() + masks_offset, mask_count * 4);

    constexpr ChannelRole mask_roles[] = {ChannelRole::R, ChannelRole::G, ChannelRole::B, ChannelRole::A};

    layout.roles.fill(ChannelRole::Unused);
    for (std::size_t m = 0; m < 4; ++m) {
        if (masks[m] == 0)
            continue;
        bool found = false;
        for (std::size_t byte = 0; byte < 4; ++byte) {
            if (masks[m] == 0xFFu << (8 * byte) && layout.roles[byte] == ChannelRole::Unused) {
                layout.roles[byte] = mask_roles[m];
                found = true;
                break;
            }
        }
        if (!found)
            return make_error(UnsupportedFormatError, "Only byte-aligned 8-bit BMP bitfield masks are supported");
    }
    return {};
}

std::span<const std::uint8_t> BmpSource::header_bytes() const {
    if (!this->map.is_mapped())
        return {};
    // file and DIB headers, masks and palette: everything up to the pixel array (checked against the size in open())
    return {reinterpret_cast<const std::uint8_t*>(this->map.data()), this->header.file_offset_to_pixel_array};
}

std::span<const std::uint8_t> BmpSource::mapped_pixels() {
    if (!this->map.is_mapped())
        return {};
    const auto* data = reinterpret_cast<const std::uint8_t*>(this->map.data()) + header.file_offset_to_pixel_array;
    return {data, this->image_info.layout.stride * this->image_info.height};
}

std::expected<void, enim::Error> BmpSource::decode(RowSink& sink, std::stop_token stop) {
    std::span<const std::uint8_t> pixels = this->mapped_pixels();
    std::size_t stride = this->image_info.layout.stride;
    std::size_t height = this->image_info.height;

    for (std::size_t y0 = 0; y0 < height; y0 += kRowsPerChunk) {
        if (stop.stop_requested()) {
            auto error = make_error(DecodeCancelled, "decode cancelled");
            sink.on_done(error);
            return error;
        }
        std::size_t count = std::min(kRowsPerChunk, height - y0);
        sink.on_rows(y0, count, pixels.data() + y0 * stride, stride);
    }

    sink.on_done({});
    return {};
}

} // namespace enim
