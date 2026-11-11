#include "qoi_source.hpp"

#include <array>
#include <cstring>
#include <vector>

namespace {

constexpr std::size_t kHeaderSize = 14;
constexpr std::size_t kEndMarkerSize = 8;     // 7 x 0x00, 0x01
constexpr std::uint64_t kMaxPixels = 400'000'000; // limit set by the specification
constexpr std::size_t kRowsPerChunk = 64;

constexpr std::uint8_t kOpIndex = 0x00; // 00xxxxxx
constexpr std::uint8_t kOpDiff = 0x40;  // 01xxxxxx
constexpr std::uint8_t kOpLuma = 0x80;  // 10xxxxxx
constexpr std::uint8_t kOpRun = 0xC0;   // 11xxxxxx
constexpr std::uint8_t kOpRgb = 0xFE;
constexpr std::uint8_t kOpRgba = 0xFF;
constexpr std::uint8_t kMask2 = 0xC0;

std::uint32_t read_be32(const std::uint8_t* p) {
    return (std::uint32_t{p[0]} << 24) | (std::uint32_t{p[1]} << 16) | (std::uint32_t{p[2]} << 8) | std::uint32_t{p[3]};
}

struct Rgba {
    std::uint8_t r = 0, g = 0, b = 0, a = 255;
};

std::size_t hash(const Rgba& px) { return (px.r * 3 + px.g * 5 + px.b * 7 + px.a * 11) % 64; }

} // namespace

namespace enim {

std::expected<void, enim::Error> QoiSource::open(const std::filesystem::path& path) {
    std::error_code ec;
    this->map.map(path.string(), ec);
    if (ec)
        return make_error(FileOpenError, ec.message());
    if (this->map.size() < kHeaderSize + kEndMarkerSize)
        return make_error(CorruptImageError, "QOI file too small");

    const auto* data = reinterpret_cast<const std::uint8_t*>(this->map.data());
    if (std::memcmp(data, "qoif", 4) != 0)
        return make_error(CorruptImageError, "Invalid QOI signature");

    std::uint32_t width = read_be32(data + 4);
    std::uint32_t height = read_be32(data + 8);
    std::uint8_t channels = data[12];
    if (width == 0 || height == 0 || std::uint64_t{width} * height > kMaxPixels)
        return make_error(CorruptImageError, "Invalid QOI dimensions");
    if (channels != 3 && channels != 4)
        return make_error(CorruptImageError, "Invalid QOI channel count");
    // data[13] is the colorspace (sRGB with linear alpha, or all linear): informative only

    this->image_info.width = width;
    this->image_info.height = height;
    PixelLayout& layout = this->image_info.layout;
    layout.sample = SampleType::U8;
    layout.channels = channels;
    layout.roles[0] = ChannelRole::R;
    layout.roles[1] = ChannelRole::G;
    layout.roles[2] = ChannelRole::B;
    if (channels == 4)
        layout.roles[3] = ChannelRole::A;
    layout.stride = std::size_t{width} * channels;
    layout.rows = RowOrder::TopDown;
    layout.big_endian = false;
    return {};
}

std::span<const std::uint8_t> QoiSource::header_bytes() const {
    if (!this->map.is_mapped())
        return {};
    return {reinterpret_cast<const std::uint8_t*>(this->map.data()), kHeaderSize};
}

std::expected<void, enim::Error> QoiSource::decode(RowSink& sink, std::stop_token stop) {
    const auto* p = reinterpret_cast<const std::uint8_t*>(this->map.data()) + kHeaderSize;
    // every op is at most 5 bytes: checking for 5 bytes before each op keeps reads inside the file
    // (a valid file always has them, since the 8-byte end marker follows the last op)
    const auto* file_end = reinterpret_cast<const std::uint8_t*>(this->map.data()) + this->map.size();

    const std::size_t height = this->image_info.height;
    const std::size_t channels = this->image_info.layout.channels;
    const std::size_t stride = this->image_info.layout.stride;

    std::vector<std::uint8_t> chunk(std::min(kRowsPerChunk, height) * stride);
    std::array<Rgba, 64> index{};
    Rgba px;
    std::size_t run = 0;

    auto fail = [&](std::unexpected<enim::Error> error) -> std::expected<void, enim::Error> {
        sink.on_done(error);
        return error;
    };

    for (std::size_t y0 = 0; y0 < height; y0 += kRowsPerChunk) {
        if (stop.stop_requested())
            return fail(make_error(DecodeCancelled, "decode cancelled"));

        const std::size_t rows = std::min(kRowsPerChunk, height - y0);
        std::uint8_t* out = chunk.data();
        std::uint8_t* const out_end = out + rows * stride;

        for (; out < out_end; out += channels) {
            if (run > 0) {
                --run;
            } else {
                if (file_end - p < 5) {
                    // keep the complete rows of this chunk
                    if (std::size_t complete = static_cast<std::size_t>(out - chunk.data()) / stride; complete > 0)
                        sink.on_rows(y0, complete, chunk.data(), stride);
                    return fail(make_error(CorruptImageError, "QOI data is truncated"));
                }
                const std::uint8_t b1 = *p++;
                if (b1 == kOpRgb) {
                    px.r = p[0];
                    px.g = p[1];
                    px.b = p[2];
                    p += 3;
                } else if (b1 == kOpRgba) {
                    px.r = p[0];
                    px.g = p[1];
                    px.b = p[2];
                    px.a = p[3];
                    p += 4;
                } else if ((b1 & kMask2) == kOpIndex) {
                    px = index[b1];
                } else if ((b1 & kMask2) == kOpDiff) {
                    px.r += ((b1 >> 4) & 0x03) - 2;
                    px.g += ((b1 >> 2) & 0x03) - 2;
                    px.b += (b1 & 0x03) - 2;
                } else if ((b1 & kMask2) == kOpLuma) {
                    const std::uint8_t b2 = *p++;
                    const int vg = (b1 & 0x3F) - 32;
                    px.r += vg - 8 + ((b2 >> 4) & 0x0F);
                    px.g += vg;
                    px.b += vg - 8 + (b2 & 0x0F);
                } else { // kOpRun
                    run = b1 & 0x3F;
                }
                index[hash(px)] = px;
            }

            out[0] = px.r;
            out[1] = px.g;
            out[2] = px.b;
            if (channels == 4)
                out[3] = px.a;
        }

        sink.on_rows(y0, rows, chunk.data(), stride);
    }

    sink.on_done({});
    return {};
}

} // namespace enim
