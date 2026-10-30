#include "jpeg_source.hpp"

// jpeglib.h uses FILE and size_t without including their headers
#include <cstddef>
#include <cstdio>

#include <array>
#include <climits>
#include <csetjmp>
#include <string>
#include <vector>

#include <jerror.h>
#include <jpeglib.h>
#include <plog/Log.h>

// libjpeg reports fatal errors through error_exit, which must not return: it longjmps back to the setjmp in the caller.
// A longjmp skips C++ destructors, so in the functions calling setjmp every object with a destructor is created
// before the setjmp, and none is alive while libjpeg runs.

namespace {

constexpr std::size_t kRowsPerChunk = 64; // a multiple of every MCU height (8 or 16 rows)

struct ErrorManager {
    jpeg_error_mgr pub; // first member: libjpeg only sees this part
    std::jmp_buf jump;
    char message[JMSG_LENGTH_MAX];
    bool fatal_eof = true; // single-scan files stop at the end of the data, the rows not decoded stay transparent
    bool truncated = false;
};

void error_exit(j_common_ptr cinfo) {
    auto* err = reinterpret_cast<ErrorManager*>(cinfo->err);
    (*cinfo->err->format_message)(cinfo, err->message);
    std::longjmp(err->jump, 1);
}

void emit_message(j_common_ptr cinfo, int msg_level) {
    if (msg_level >= 0) // trace messages
        return;
    auto* err = reinterpret_cast<ErrorManager*>(cinfo->err);
    if (cinfo->err->msg_code == JWRN_JPEG_EOF) {
        // libjpeg fills the missing data and goes on. Multi-scan files have every block from the first scans,
        // so they are shown with less detail and the error is reported at the end.
        if (err->fatal_eof)
            error_exit(cinfo);
        err->truncated = true;
    }
    // corrupt data warnings can repeat for every block: only the first is logged
    if (cinfo->err->num_warnings++ == 0) {
        char message[JMSG_LENGTH_MAX];
        (*cinfo->err->format_message)(cinfo, message);
        PLOG_WARNING << "JPEG: " << message;
    }
}

/// @brief libjpeg decompressor reading from memory. jpeg_create_decompress is called after the setjmp, since it can
/// fail; destroying a decompressor that was never created does nothing.
struct Decompressor {
    jpeg_decompress_struct cinfo{};
    ErrorManager err{};

    Decompressor() {
        this->cinfo.err = jpeg_std_error(&this->err.pub);
        this->err.pub.error_exit = error_exit;
        this->err.pub.emit_message = emit_message;
    }
    ~Decompressor() { jpeg_destroy_decompress(&this->cinfo); }
    Decompressor(const Decompressor&) = delete;
    Decompressor& operator=(const Decompressor&) = delete;
};

enim::Error jpeg_error(const char* message) {
    return enim::Error{enim::ErrorCode::FileOpenError, std::string("JPEG: ") + message};
}

/// @brief Creates the decompressor, reads the header and chooses the output color space.
/// Returns nullptr, or the reason the image is not supported. Must be called after the setjmp.
const char* start(Decompressor& d, const mio::mmap_source& map) {
    jpeg_create_decompress(&d.cinfo);
    jpeg_mem_src(&d.cinfo, reinterpret_cast<const unsigned char*>(map.data()), static_cast<unsigned long>(map.size()));
    jpeg_read_header(&d.cinfo, TRUE);
    d.err.fatal_eof = !jpeg_has_multiple_scans(&d.cinfo);

    if (d.cinfo.data_precision != 8)
        return "12-bit and 16-bit images are not supported yet";
    switch (d.cinfo.jpeg_color_space) {
    case JCS_GRAYSCALE: d.cinfo.out_color_space = JCS_GRAYSCALE; break;
    case JCS_YCbCr:
    case JCS_RGB: d.cinfo.out_color_space = JCS_EXT_RGB; break;
    case JCS_CMYK:
    case JCS_YCCK: return "CMYK images are not supported yet";
    default: return "unknown color space";
    }
    jpeg_calc_output_dimensions(&d.cinfo);
    return nullptr;
}

} // namespace

namespace enim {

std::expected<void, enim::Error> JpegSource::open(const std::filesystem::path& path) {
    std::error_code ec;
    this->map.map(path.string(), ec);
    if (ec)
        return std::unexpected(enim::Error{enim::ErrorCode::FileOpenError, ec.message()});
    if (this->map.size() > ULONG_MAX) // jpeg_mem_src takes an unsigned long, 32-bit on Windows
        return std::unexpected(jpeg_error("file too large"));

    Decompressor d;
    if (setjmp(d.err.jump))
        return std::unexpected(jpeg_error(d.err.message));
    if (const char* unsupported = start(d, this->map))
        return std::unexpected(jpeg_error(unsupported));

    this->image_info.width = d.cinfo.output_width;
    this->image_info.height = d.cinfo.output_height;
    PixelLayout& layout = this->image_info.layout;
    layout.sample = SampleType::U8;
    layout.channels = static_cast<std::uint8_t>(d.cinfo.output_components);
    if (layout.channels == 1) {
        layout.roles[0] = ChannelRole::Gray;
    } else {
        layout.roles[0] = ChannelRole::R;
        layout.roles[1] = ChannelRole::G;
        layout.roles[2] = ChannelRole::B;
    }
    layout.stride = this->image_info.width * layout.channels;
    layout.rows = RowOrder::TopDown;
    layout.big_endian = false;
    return {};
}

std::expected<void, enim::Error> JpegSource::decode(RowSink& sink, std::stop_token stop) {
    const std::size_t height = this->image_info.height;
    const std::size_t stride = this->image_info.layout.stride;

    auto fail = [&](enim::Error error) -> std::expected<void, enim::Error> {
        sink.on_done(std::unexpected(error));
        return std::unexpected(error);
    };

    Decompressor d;
    std::vector<std::uint8_t> chunk(std::min(kRowsPerChunk, height) * stride);
    std::array<JSAMPROW, kRowsPerChunk> row_pointers{};
    for (std::size_t i = 0; i < row_pointers.size() && i * stride < chunk.size(); ++i)
        row_pointers[i] = chunk.data() + i * stride;

    // written after the setjmp and read after the longjmp
    volatile std::size_t y0 = 0;
    volatile std::size_t rows_done = 0;

    if (setjmp(d.err.jump)) {
        // keep the complete rows of this chunk
        if (rows_done > 0)
            sink.on_rows(y0, rows_done, chunk.data(), stride);
        return fail(jpeg_error(d.err.message));
    }
    if (const char* unsupported = start(d, this->map))
        return fail(jpeg_error(unsupported));
    if (d.cinfo.output_width != this->image_info.width || d.cinfo.output_height != height
        || static_cast<std::size_t>(d.cinfo.output_components) != this->image_info.layout.channels)
        return fail(jpeg_error("header changed since open()"));

    // multi-scan (progressive) files are read whole here, the rows are produced afterwards
    jpeg_start_decompress(&d.cinfo);

    for (std::size_t y = 0; y < height; y += kRowsPerChunk) {
        if (stop.stop_requested())
            return fail(enim::Error{enim::ErrorCode::ImageLoadError, "decode cancelled"});

        y0 = y;
        const std::size_t rows = std::min(kRowsPerChunk, height - y);
        for (std::size_t done = 0; done < rows;) {
            JDIMENSION read = jpeg_read_scanlines(&d.cinfo, row_pointers.data() + done, static_cast<JDIMENSION>(rows - done));
            if (read == 0) // only a suspending data source returns 0, and jpeg_mem_src never suspends
                return fail(jpeg_error("no rows decoded"));
            done += read;
            rows_done = done;
        }
        sink.on_rows(y, rows, chunk.data(), stride);
        rows_done = 0;
    }

    // jpeg_finish_decompress is not called: it only reads the markers after the image data, and a missing end
    // marker would make it fail although every row is there. The destructor releases the decompressor.
    if (d.err.truncated)
        return fail(jpeg_error("premature end of data, the image is incomplete"));

    sink.on_done({});
    return {};
}

} // namespace enim
