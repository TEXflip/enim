#include "webp_source.hpp"

#include <algorithm>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <vector>

#include <plog/Log.h>
#include <webp/decode.h>
#include <webp/demux.h>

namespace {

constexpr std::size_t kMinFeedBytes = std::size_t{256} << 10; // bytes given to the decoder per step, at least
constexpr std::size_t kFeedSteps = 64;                        // about this many steps per image
constexpr std::size_t kRowsPerChunk = 64;                     // composed rows of a frame smaller than the canvas

const char* status_message(VP8StatusCode status) {
    switch (status) {
    case VP8_STATUS_OK: return "ok";
    case VP8_STATUS_OUT_OF_MEMORY: return "out of memory";
    case VP8_STATUS_INVALID_PARAM: return "invalid parameter";
    case VP8_STATUS_BITSTREAM_ERROR: return "bitstream error";
    case VP8_STATUS_UNSUPPORTED_FEATURE: return "unsupported feature";
    case VP8_STATUS_SUSPENDED: return "premature end of data, the image is incomplete";
    case VP8_STATUS_USER_ABORT: return "aborted";
    case VP8_STATUS_NOT_ENOUGH_DATA: return "not enough data";
    }
    return "unknown error";
}

std::string webp_message(VP8StatusCode status) { return std::string("WebP: ") + status_message(status); }

std::uint32_t read_le32(const std::uint8_t* p) {
    return std::uint32_t{p[0]} | (std::uint32_t{p[1]} << 8) | (std::uint32_t{p[2]} << 16) | (std::uint32_t{p[3]} << 24);
}

/// @brief Bytes before the image data: the RIFF header, the chunks before the first VP8/VP8L/ANMF chunk (VP8X,
/// ICCP, ANIM, ...) and the start of that chunk, which holds the frame header with the dimensions.
std::size_t webp_header_size(std::span<const std::uint8_t> file) {
    constexpr std::size_t kBitstreamHeader = 32;
    std::size_t pos = 12; // "RIFF", size, "WEBP"
    while (pos + 8 <= file.size()) {
        const std::uint8_t* chunk = file.data() + pos;
        const std::size_t size = read_le32(chunk + 4);
        if (std::memcmp(chunk, "VP8 ", 4) == 0 || std::memcmp(chunk, "VP8L", 4) == 0 || std::memcmp(chunk, "ANMF", 4) == 0)
            return std::min(file.size(), pos + 8 + std::min(size, kBitstreamHeader));
        pos += 8 + size + (size & 1); // chunks are padded to an even size
    }
    return std::min(file.size(), pos);
}

using Decoder = std::unique_ptr<WebPIDecoder, decltype(&WebPIDelete)>;

} // namespace

namespace enim {

std::expected<void, enim::Error> WebpSource::open(const std::filesystem::path& path) {
    std::error_code ec;
    this->map.map(path.string(), ec);
    if (ec)
        return make_error(FileOpenError, ec.message());
    const auto* data = reinterpret_cast<const std::uint8_t*>(this->map.data());
    const std::size_t size = this->map.size();

    WebPBitstreamFeatures features{};
    if (VP8StatusCode status = WebPGetFeatures(data, size, &features); status != VP8_STATUS_OK)
        return make_error(CorruptImageError, webp_message(status));

    std::size_t canvas_width = static_cast<std::size_t>(features.width);
    std::size_t canvas_height = static_cast<std::size_t>(features.height);
    bool alpha = features.has_alpha != 0;
    this->bitstream_offset = 0;
    this->bitstream_size = size;
    this->frame_x = this->frame_y = 0;

    if (features.has_animation) {
        // the first frame, placed on the canvas; a truncated file still gives the frames it has
        WebPData webp_data{data, size};
        WebPDemuxState state = WEBP_DEMUX_PARSING_HEADER;
        std::unique_ptr<WebPDemuxer, decltype(&WebPDemuxDelete)> demux(WebPDemuxPartial(&webp_data, &state), WebPDemuxDelete);
        if (!demux || state == WEBP_DEMUX_PARSE_ERROR)
            return make_error(CorruptImageError, "WebP: invalid animation");
        canvas_width = WebPDemuxGetI(demux.get(), WEBP_FF_CANVAS_WIDTH);
        canvas_height = WebPDemuxGetI(demux.get(), WEBP_FF_CANVAS_HEIGHT);
        WebPIterator frame{};
        if (!WebPDemuxGetFrame(demux.get(), 1, &frame))
            return make_error(CorruptImageError, "WebP: the animation has no frame");
        PLOGI << "WebP: animation of " << frame.num_frames << " frames, showing the first one";
        this->frame_x = static_cast<std::size_t>(frame.x_offset);
        this->frame_y = static_cast<std::size_t>(frame.y_offset);
        // the fragment points into the mapped file
        this->bitstream_offset = static_cast<std::size_t>(frame.fragment.bytes - data);
        this->bitstream_size = frame.fragment.size;
        this->frame_width = static_cast<std::size_t>(frame.width);
        this->frame_height = static_cast<std::size_t>(frame.height);
        const bool covers = this->frame_x == 0 && this->frame_y == 0 && this->frame_width == canvas_width && this->frame_height == canvas_height;
        alpha = frame.has_alpha || !covers; // the canvas around the frame is transparent
        WebPDemuxReleaseIterator(&frame);
    } else {
        this->frame_width = canvas_width;
        this->frame_height = canvas_height;
    }
    if (canvas_width == 0 || canvas_height == 0 || this->frame_x + this->frame_width > canvas_width
        || this->frame_y + this->frame_height > canvas_height)
        return make_error(CorruptImageError, "WebP: invalid frame size");

    this->header_size = webp_header_size({data, size});
    this->image_info.width = canvas_width;
    this->image_info.height = canvas_height;
    PixelLayout& layout = this->image_info.layout;
    layout.sample = SampleType::U8;
    layout.channels = alpha ? 4 : 3;
    layout.roles[0] = ChannelRole::R;
    layout.roles[1] = ChannelRole::G;
    layout.roles[2] = ChannelRole::B;
    layout.roles[3] = alpha ? ChannelRole::A : ChannelRole::Unused;
    layout.stride = canvas_width * layout.channels;
    layout.rows = RowOrder::TopDown;
    layout.big_endian = false;

    PLOGI << "WebP: " << canvas_width << "x" << canvas_height << ", "
          << (features.format == 2 ? "lossless" : features.format == 1 ? "lossy" : "mixed") << (alpha ? ", alpha" : "");
    return {};
}

std::span<const std::uint8_t> WebpSource::header_bytes() const {
    if (!this->map.is_mapped())
        return {};
    return {reinterpret_cast<const std::uint8_t*>(this->map.data()), this->header_size};
}

std::expected<void, enim::Error> WebpSource::decode(RowSink& sink, std::stop_token stop) {
    const std::size_t width = this->image_info.width;
    const std::size_t height = this->image_info.height;
    const std::size_t bpp = this->image_info.layout.channels;
    const std::size_t stride = this->image_info.layout.stride;
    const std::size_t frame_stride = this->frame_width * bpp;
    const bool covers = this->frame_width == width && this->frame_height == height;

    auto fail = [&](std::unexpected<enim::Error> error) -> std::expected<void, enim::Error> {
        sink.on_done(error);
        return error;
    };

    // the decoder writes the whole frame here
    std::unique_ptr<std::uint8_t[]> frame(new (std::nothrow) std::uint8_t[this->frame_height * frame_stride]);
    if (!frame)
        return fail(make_error(ImageLoadError, "WebP: not enough memory for the image"));

    // A frame smaller than the canvas (animations) is composed into canvas rows, transparent around it.
    std::vector<std::uint8_t> composed(covers ? 0 : std::min(kRowsPerChunk, height) * stride);
    auto emit_canvas_rows = [&](std::size_t y0, std::size_t y1) { // canvas rows outside the frame
        std::fill(composed.begin(), composed.end(), std::uint8_t{0});
        for (std::size_t y = y0; y < y1; y += kRowsPerChunk)
            sink.on_rows(y, std::min(kRowsPerChunk, y1 - y), composed.data(), stride);
    };
    auto emit_frame_rows = [&](std::size_t r0, std::size_t r1) { // decoded rows [r0, r1) of the frame
        if (covers) {
            sink.on_rows(r0, r1 - r0, frame.get() + r0 * frame_stride, frame_stride);
            return;
        }
        for (std::size_t r = r0; r < r1; r += kRowsPerChunk) {
            const std::size_t rows = std::min(kRowsPerChunk, r1 - r);
            std::fill(composed.begin(), composed.end(), std::uint8_t{0});
            for (std::size_t i = 0; i < rows; ++i)
                std::memcpy(composed.data() + i * stride + this->frame_x * bpp, frame.get() + (r + i) * frame_stride, frame_stride);
            sink.on_rows(this->frame_y + r, rows, composed.data(), stride);
        }
    };

    WebPDecoderConfig config;
    if (!WebPInitDecoderConfig(&config))
        return fail(make_error(ImageLoadError, "WebP: incompatible libwebp version"));
    config.options.use_threads = 1; // lossy: filtering on a second thread
    config.output.colorspace = bpp == 4 ? MODE_RGBA : MODE_RGB;
    config.output.is_external_memory = 1;
    config.output.u.RGBA.rgba = frame.get();
    config.output.u.RGBA.stride = static_cast<int>(frame_stride);
    config.output.u.RGBA.size = this->frame_height * frame_stride;
    Decoder decoder(WebPIDecode(nullptr, 0, &config), WebPIDelete);
    if (!decoder)
        return fail(make_error(ImageLoadError, "WebP: couldn't create the decoder"));

    if (!covers)
        emit_canvas_rows(0, this->frame_y);

    // The data is all mapped: WebPIUpdate sees a growing prefix of it, so rows come out while the rest is decoded.
    const auto* bitstream = reinterpret_cast<const std::uint8_t*>(this->map.data()) + this->bitstream_offset;
    const std::size_t step = std::max(kMinFeedBytes, this->bitstream_size / kFeedSteps);
    std::size_t fed = 0;
    std::size_t emitted = 0;
    VP8StatusCode status = VP8_STATUS_SUSPENDED;
    while (status == VP8_STATUS_SUSPENDED && fed < this->bitstream_size) {
        if (stop.stop_requested())
            return fail(make_error(DecodeCancelled, "decode cancelled"));
        fed = std::min(this->bitstream_size, fed + step);
        status = WebPIUpdate(decoder.get(), bitstream, fed);
        int last_y = 0;
        if (WebPIDecGetRGB(decoder.get(), &last_y, nullptr, nullptr, nullptr) && static_cast<std::size_t>(last_y) > emitted) {
            emit_frame_rows(emitted, static_cast<std::size_t>(last_y));
            emitted = static_cast<std::size_t>(last_y);
        }
    }
    // SUSPENDED with every byte given: the file is truncated; the rows decoded so far stay
    if (status != VP8_STATUS_OK)
        return fail(make_error(CorruptImageError, webp_message(status)));
    if (emitted < this->frame_height)
        emit_frame_rows(emitted, this->frame_height);
    if (!covers)
        emit_canvas_rows(this->frame_y + this->frame_height, height);

    sink.on_done({});
    return {};
}

} // namespace enim
