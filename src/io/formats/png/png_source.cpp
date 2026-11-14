#include "png_source.hpp"

#include <algorithm>
#include <cstring>
#include <memory>
#include <new>
#include <string>

#include <plog/Log.h>
#include <spng.h>

namespace {

constexpr std::size_t kRowsPerChunk = 64;

using Context = std::unique_ptr<spng_ctx, decltype(&spng_ctx_free)>;

std::string spng_message(int code) { return std::string("PNG: ") + spng_strerror(code); }

/// @brief A decoder reading the mapped file. Checksums are not verified: CRC-32 of every chunk and Adler-32 of the
/// image data would cost a few percent of the decode time, and a corrupt stream still fails in inflate.
std::expected<Context, enim::Error> make_context(const mio::mmap_source& map) {
    Context ctx(spng_ctx_new(SPNG_CTX_IGNORE_ADLER32), spng_ctx_free);
    if (!ctx)
        return enim::make_error(enim::ImageLoadError, "PNG: couldn't create the decoder");
    if (int err = spng_set_crc_action(ctx.get(), SPNG_CRC_USE, SPNG_CRC_USE))
        return enim::make_error(enim::ImageLoadError, spng_message(err));
    if (int err = spng_set_png_buffer(ctx.get(), map.data(), map.size()))
        return enim::make_error(enim::CorruptImageError, spng_message(err));
    return ctx;
}

std::uint32_t read_be32(const std::uint8_t* p) {
    return (std::uint32_t{p[0]} << 24) | (std::uint32_t{p[1]} << 16) | (std::uint32_t{p[2]} << 8) | std::uint32_t{p[3]};
}

/// @brief Bytes before the image data: the signature and every chunk before the first IDAT, plus the IDAT's length
/// and type. The whole file if there is no IDAT (the decoder fails on it anyway).
std::size_t png_header_size(std::span<const std::uint8_t> file) {
    std::size_t pos = 8; // signature
    while (pos + 8 <= file.size()) {
        if (std::memcmp(file.data() + pos + 4, "IDAT", 4) == 0)
            return pos + 8;
        const std::size_t next = pos + 12 + read_be32(file.data() + pos); // length, type, data, CRC
        if (next > file.size())
            break;
        pos = next;
    }
    return file.size();
}

} // namespace

namespace enim {

std::expected<void, enim::Error> PngSource::open(const std::filesystem::path& path) {
    std::error_code ec;
    this->map.map(path.string(), ec);
    if (ec)
        return make_error(FileOpenError, ec.message());

    auto ctx = make_context(this->map);
    if (!ctx)
        return std::unexpected(ctx.error());

    spng_ihdr ihdr{};
    if (int err = spng_get_ihdr(ctx->get(), &ihdr))
        return make_error(CorruptImageError, spng_message(err));
    // reads the chunks up to the first IDAT; SPNG_ECHUNKAVAIL if there is no tRNS
    spng_trns trns{};
    const bool has_trns = spng_get_trns(ctx->get(), &trns) == 0;

    // The output format keeps the PNG's layout whenever it is a whole number of bytes per sample (SPNG_FMT_PNG:
    // 16-bit samples in host byte order). Palette and 1/2/4-bit images are expanded, and tRNS needs an alpha channel.
    const bool depth16 = ihdr.bit_depth == 16;
    SampleType sample = depth16 ? SampleType::U16 : SampleType::U8;
    std::uint8_t channels = 0;
    this->decode_flags = 0;
    switch (ihdr.color_type) {
    case SPNG_COLOR_TYPE_GRAYSCALE:
        if (has_trns) {
            this->format = depth16 ? SPNG_FMT_GA16 : SPNG_FMT_GA8;
            this->decode_flags = SPNG_DECODE_TRNS;
            channels = 2;
        } else {
            this->format = ihdr.bit_depth >= 8 ? SPNG_FMT_PNG : SPNG_FMT_G8; // G8 scales 1/2/4-bit values to 0..255
            channels = 1;
        }
        break;
    case SPNG_COLOR_TYPE_TRUECOLOR:
        if (has_trns) {
            this->format = depth16 ? SPNG_FMT_RGBA16 : SPNG_FMT_RGBA8;
            this->decode_flags = SPNG_DECODE_TRNS;
            channels = 4;
        } else {
            this->format = SPNG_FMT_PNG;
            channels = 3;
        }
        break;
    case SPNG_COLOR_TYPE_INDEXED:
        sample = SampleType::U8;
        this->format = has_trns ? SPNG_FMT_RGBA8 : SPNG_FMT_RGB8;
        this->decode_flags = has_trns ? SPNG_DECODE_TRNS : 0;
        channels = has_trns ? 4 : 3;
        break;
    case SPNG_COLOR_TYPE_GRAYSCALE_ALPHA:
        this->format = SPNG_FMT_PNG;
        channels = 2;
        break;
    case SPNG_COLOR_TYPE_TRUECOLOR_ALPHA:
        this->format = SPNG_FMT_PNG;
        channels = 4;
        break;
    default: return make_error(CorruptImageError, "PNG: invalid color type");
    }

    std::size_t image_bytes = 0;
    if (int err = spng_decoded_image_size(ctx->get(), this->format, &image_bytes))
        return make_error(CorruptImageError, spng_message(err));

    this->interlaced = ihdr.interlace_method != SPNG_INTERLACE_NONE;
    this->header_size = png_header_size({reinterpret_cast<const std::uint8_t*>(this->map.data()), this->map.size()});
    this->image_info.width = ihdr.width;
    this->image_info.height = ihdr.height;
    PixelLayout& layout = this->image_info.layout;
    layout.sample = sample;
    layout.channels = channels;
    if (channels <= 2) {
        layout.roles[0] = ChannelRole::Gray;
    } else {
        layout.roles[0] = ChannelRole::R;
        layout.roles[1] = ChannelRole::G;
        layout.roles[2] = ChannelRole::B;
    }
    if (channels == 2 || channels == 4)
        layout.roles[channels - 1] = ChannelRole::A;
    layout.stride = this->image_info.width * layout.bytes_per_pixel();
    layout.rows = RowOrder::TopDown;
    layout.big_endian = false; // every output format is host-endian
    if (image_bytes != layout.stride * this->image_info.height)
        return make_error(ImageLoadError, "PNG: unexpected decoded image size");
    return {};
}

std::span<const std::uint8_t> PngSource::header_bytes() const {
    if (!this->map.is_mapped())
        return {};
    return {reinterpret_cast<const std::uint8_t*>(this->map.data()), this->header_size};
}

std::expected<void, enim::Error> PngSource::decode(RowSink& sink, std::stop_token stop) {
    const std::size_t height = this->image_info.height;
    const std::size_t stride = this->image_info.layout.stride;

    auto fail = [&](std::unexpected<enim::Error> error) -> std::expected<void, enim::Error> {
        sink.on_done(error);
        return error;
    };

    // a new decoder: a context decodes the image only once
    auto ctx = make_context(this->map);
    if (!ctx)
        return fail(std::unexpected(ctx.error()));
    spng_ihdr ihdr{};
    if (int err = spng_get_ihdr(ctx->get(), &ihdr))
        return fail(make_error(CorruptImageError, spng_message(err)));
    if (ihdr.width != this->image_info.width || ihdr.height != height)
        return fail(make_error(ImageLoadError, "PNG: header changed since open()"));
    if (int err = spng_decode_image(ctx->get(), nullptr, 0, this->format, this->decode_flags | SPNG_DECODE_PROGRESSIVE))
        return fail(make_error(CorruptImageError, spng_message(err)));

    if (!this->interlaced) {
        std::unique_ptr<std::uint8_t[]> chunk(new (std::nothrow) std::uint8_t[std::min(kRowsPerChunk, height) * stride]);
        if (!chunk)
            return fail(make_error(ImageLoadError, "PNG: out of memory"));
        for (std::size_t y = 0; y < height; y += kRowsPerChunk) {
            if (stop.stop_requested())
                return fail(make_error(DecodeCancelled, "decode cancelled"));
            const std::size_t rows = std::min(kRowsPerChunk, height - y);
            for (std::size_t i = 0; i < rows; ++i) {
                // SPNG_EOI: the last row was decoded
                int err = spng_decode_row(ctx->get(), chunk.get() + i * stride, stride);
                if (err != 0 && err != SPNG_EOI) {
                    // keep the complete rows of this chunk
                    if (i > 0)
                        sink.on_rows(y, i, chunk.get(), stride);
                    return fail(make_error(CorruptImageError, spng_message(err)));
                }
            }
            sink.on_rows(y, rows, chunk.get(), stride);
        }
        sink.on_done({});
        return {};
    }

    // Interlaced: the 7 passes write every row several times, so the image is decoded whole first. A row is final
    // only after the 6th pass (even rows) or the 7th (odd rows), so nothing could be shown much earlier anyway.
    std::unique_ptr<std::uint8_t[]> image(new (std::nothrow) std::uint8_t[height * stride]);
    if (!image)
        return fail(make_error(ImageLoadError, "PNG: not enough memory to deinterlace the image"));
    for (std::size_t n = 0;; ++n) {
        if (n % kRowsPerChunk == 0 && stop.stop_requested())
            return fail(make_error(DecodeCancelled, "decode cancelled"));
        spng_row_info row{};
        int err = spng_get_row_info(ctx->get(), &row);
        if (err == 0)
            err = spng_decode_row(ctx->get(), image.get() + std::size_t{row.row_num} * stride, stride);
        if (err == SPNG_EOI)
            break;
        if (err != 0) // the rows are incomplete until the last passes: nothing is emitted
            return fail(make_error(CorruptImageError, spng_message(err)));
    }
    for (std::size_t y = 0; y < height; y += kRowsPerChunk) {
        if (stop.stop_requested())
            return fail(make_error(DecodeCancelled, "decode cancelled"));
        sink.on_rows(y, std::min(kRowsPerChunk, height - y), image.get() + y * stride, stride);
    }
    sink.on_done({});
    return {};
}

} // namespace enim
