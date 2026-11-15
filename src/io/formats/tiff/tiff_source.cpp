#include "tiff_source.hpp"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_set>

#include <plog/Log.h>
#include <tiffio.h>

#include <core/thread_pool.hpp>

namespace {

using enim::Error;
using enim::tiff::ColorOp;
using enim::tiff::InFormat;

constexpr std::size_t kBandRows = 64;                   // rows per band, at least
constexpr std::size_t kBandBytes = std::size_t{64} << 20; // decoded bytes per band, at most (when it has several units)
constexpr std::uint64_t kScanlineStripBytes = std::uint64_t{16} << 20; // larger strips are read row by row
constexpr std::size_t kRawRowsPerChunk = 256;
constexpr std::size_t kPad = 8; // the bit unpacker reads whole 64-bit words
constexpr std::uint16_t kPhotometricLinearRaw = 34892; // DNG

/// @brief A libtiff handle over the mapped file, with its own error state. libtiff handles are not thread-safe:
/// each decode worker has its own.
struct Handle {
    const std::uint8_t* data = nullptr;
    std::uint64_t size = 0;
    std::uint64_t pos = 0;
    std::string error; // first error reported since the last take_error()
    std::unordered_set<std::string> warnings;
    TIFF* tif = nullptr;
    std::vector<std::uint8_t> scratch; // one tile

    Handle() = default;
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    ~Handle() {
        if (this->tif)
            TIFFClose(this->tif);
    }

    std::string take_error(const char* fallback) {
        std::string message = "TIFF: " + (this->error.empty() ? std::string(fallback) : this->error);
        this->error.clear();
        return message;
    }
};

tmsize_t read_proc(thandle_t handle, void* buf, tmsize_t n) {
    auto* h = static_cast<Handle*>(handle);
    if (n <= 0 || h->pos >= h->size)
        return 0;
    const std::uint64_t count = std::min<std::uint64_t>(static_cast<std::uint64_t>(n), h->size - h->pos);
    std::memcpy(buf, h->data + h->pos, count);
    h->pos += count;
    return static_cast<tmsize_t>(count);
}

tmsize_t write_proc(thandle_t, void*, tmsize_t) { return 0; }

toff_t seek_proc(thandle_t handle, toff_t offset, int whence) {
    auto* h = static_cast<Handle*>(handle);
    switch (whence) {
    case SEEK_SET: h->pos = offset; break;
    case SEEK_CUR: h->pos += offset; break;
    case SEEK_END: h->pos = h->size + offset; break;
    default: return static_cast<toff_t>(-1);
    }
    return h->pos;
}

int close_proc(thandle_t) { return 0; }
toff_t size_proc(thandle_t handle) { return static_cast<Handle*>(handle)->size; }

/// @brief The file is already mapped: libtiff reads uncompressed strips and tiles in place.
int map_proc(thandle_t handle, void** base, toff_t* size) {
    auto* h = static_cast<Handle*>(handle);
    *base = const_cast<std::uint8_t*>(h->data);
    *size = h->size;
    return 1;
}

void unmap_proc(thandle_t, void*, toff_t) {}

std::string format_message(const char* module, const char* fmt, va_list ap) {
    char buffer[1024];
    std::vsnprintf(buffer, sizeof(buffer), fmt, ap);
    return module && *module ? std::string(module) + ": " + buffer : std::string(buffer);
}

int error_handler(TIFF*, void* user, const char* module, const char* fmt, va_list ap) {
    auto* h = static_cast<Handle*>(user);
    std::string message = format_message(module, fmt, ap);
    if (h->error.empty())
        h->error = std::move(message);
    else
        PLOGD << "TIFF: " << message;
    return 1;
}

int warning_handler(TIFF*, void* user, const char* module, const char* fmt, va_list ap) {
    auto* h = static_cast<Handle*>(user);
    std::string message = format_message(module, fmt, ap);
    if (h->warnings.insert(message).second) // every distinct warning once
        PLOGW << "TIFF: " << message;
    return 1;
}

/// @brief Opens the file and selects the directory; jpeg_rgb and sgilog_float set the decoders' output format.
std::expected<std::unique_ptr<Handle>, Error> open_handle(const mio::mmap_source& map, const std::string& name,
                                                          tdir_t directory, bool jpeg_rgb, bool sgilog_float) {
    auto h = std::make_unique<Handle>();
    h->data = reinterpret_cast<const std::uint8_t*>(map.data());
    h->size = map.size();

    TIFFOpenOptions* options = TIFFOpenOptionsAlloc();
    if (!options)
        return enim::make_error(enim::ImageLoadError, "TIFF: out of memory");
    TIFFOpenOptionsSetErrorHandlerExtR(options, error_handler, h.get());
    TIFFOpenOptionsSetWarningHandlerExtR(options, warning_handler, h.get());
    h->tif = TIFFClientOpenExt(name.c_str(), "r", h.get(), read_proc, write_proc, seek_proc, close_proc, size_proc,
                               map_proc, unmap_proc, options);
    TIFFOpenOptionsFree(options);
    if (!h->tif)
        return enim::make_error(enim::CorruptImageError, h->take_error("couldn't read the header"));

    if (directory != 0 && !TIFFSetDirectory(h->tif, directory))
        return enim::make_error(enim::CorruptImageError, h->take_error("couldn't read the image directory"));
    if (jpeg_rgb && !TIFFSetField(h->tif, TIFFTAG_JPEGCOLORMODE, JPEGCOLORMODE_RGB))
        return enim::make_error(enim::ImageLoadError, h->take_error("couldn't set the JPEG color mode"));
    if (sgilog_float && !TIFFSetField(h->tif, TIFFTAG_SGILOGDATAFMT, SGILOGDATAFMT_FLOAT))
        return enim::make_error(enim::ImageLoadError, h->take_error("couldn't set the LogLuv data format"));
    return h;
}

template <typename T>
T field_defaulted(TIFF* tif, ttag_t tag, T fallback) {
    T value = fallback;
    if (!TIFFGetFieldDefaulted(tif, tag, &value))
        return fallback;
    return value;
}

const char* photometric_name(std::uint16_t p) {
    switch (p) {
    case PHOTOMETRIC_MINISWHITE: return "MinIsWhite";
    case PHOTOMETRIC_MINISBLACK: return "MinIsBlack";
    case PHOTOMETRIC_RGB: return "RGB";
    case PHOTOMETRIC_PALETTE: return "Palette";
    case PHOTOMETRIC_MASK: return "Mask";
    case PHOTOMETRIC_SEPARATED: return "Separated";
    case PHOTOMETRIC_YCBCR: return "YCbCr";
    case PHOTOMETRIC_CIELAB: return "CIELab";
    case PHOTOMETRIC_ICCLAB: return "ICCLab";
    case PHOTOMETRIC_ITULAB: return "ITULab";
    case PHOTOMETRIC_CFA: return "CFA";
    case PHOTOMETRIC_LOGL: return "LogL";
    case PHOTOMETRIC_LOGLUV: return "LogLuv";
    case kPhotometricLinearRaw: return "LinearRaw";
    default: return "unknown";
    }
}

const char* path_label(enim::TiffSource::Path path) {
    switch (path) {
    case enim::TiffSource::Path::Raw: return "raw (mmap)";
    case enim::TiffSource::Path::Native: return "decoded, native layout";
    case enim::TiffSource::Path::Convert: return "decoded and converted";
    case enim::TiffSource::Path::Rgba: return "RGBA (TIFFRGBAImage)";
    }
    return "?";
}

std::uint64_t read_uint(const std::uint8_t* p, std::size_t bytes, bool big_endian) {
    std::uint64_t v = 0;
    for (std::size_t i = 0; i < bytes; ++i)
        v |= std::uint64_t{p[i]} << (8 * (big_endian ? bytes - 1 - i : i));
    return v;
}

/// @brief File header + the raw bytes of the IFD at offset (entry count, entries, next IFD offset).
std::vector<std::uint8_t> header_and_ifd(std::span<const std::uint8_t> file, std::uint64_t offset, bool big_tiff, bool big_endian) {
    const std::size_t header_size = big_tiff ? 16 : 8;
    std::vector<std::uint8_t> bytes(file.begin(), file.begin() + std::min(header_size, file.size()));
    const std::size_t count_size = big_tiff ? 8 : 2;
    if (offset + count_size > file.size())
        return bytes;
    const std::uint64_t entries = read_uint(file.data() + offset, count_size, big_endian);
    const std::uint64_t size = count_size + entries * (big_tiff ? 20 : 12) + (big_tiff ? 8 : 4);
    const std::uint64_t end = std::min<std::uint64_t>(file.size(), offset + size);
    bytes.insert(bytes.end(), file.begin() + offset, file.begin() + end);
    return bytes;
}

/// @brief Handles for the decode workers, opened on first use.
struct HandlePool {
    const mio::mmap_source& map;
    const std::string& name;
    tdir_t directory;
    bool jpeg_rgb, sgilog_float;
    std::mutex mutex;
    std::vector<std::unique_ptr<Handle>> free;

    std::expected<std::unique_ptr<Handle>, Error> acquire() {
        {
            std::lock_guard lock(this->mutex);
            if (!this->free.empty()) {
                auto h = std::move(this->free.back());
                this->free.pop_back();
                return h;
            }
        }
        return open_handle(this->map, this->name, this->directory, this->jpeg_rgb, this->sgilog_float);
    }

    void release(std::unique_ptr<Handle> h) {
        std::lock_guard lock(this->mutex);
        this->free.push_back(std::move(h));
    }
};

} // namespace

namespace enim {

TiffSource::TiffSource() = default;
TiffSource::~TiffSource() = default;

std::expected<void, enim::Error> TiffSource::open(const std::filesystem::path& file_path) {
    std::error_code ec;
    this->map.map(file_path.string(), ec);
    if (ec)
        return make_error(FileOpenError, ec.message());
    this->path_name = file_path.filename().string();

    auto opened = open_handle(this->map, this->path_name, 0, false, false);
    if (!opened)
        return std::unexpected(opened.error());
    Handle& h = **opened;
    TIFF* tif = h.tif;

    // the first full-resolution image: reduced-resolution IFDs (overviews, thumbnails) are skipped
    const tdir_t directories = TIFFNumberOfDirectories(tif);
    this->directory = 0;
    for (tdir_t d = 0; d < directories; ++d) {
        if (d > 0 && !TIFFSetDirectory(tif, d))
            break;
        if (!(field_defaulted<std::uint32_t>(tif, TIFFTAG_SUBFILETYPE, 0) & FILETYPE_REDUCEDIMAGE)) {
            this->directory = d;
            break;
        }
    }
    if (TIFFCurrentDirectory(tif) != this->directory && !TIFFSetDirectory(tif, this->directory))
        return make_error(CorruptImageError, h.take_error("couldn't read the image directory"));
    if (directories > 1)
        PLOGI << "TIFF: " << directories << " images in the file, showing #" << this->directory + 1;

    std::uint32_t width = 0, height = 0;
    if (!TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &width) || !TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &height) || width == 0 || height == 0)
        return make_error(CorruptImageError, "TIFF: invalid image size");
    const std::uint16_t spp = field_defaulted<std::uint16_t>(tif, TIFFTAG_SAMPLESPERPIXEL, 1);
    const std::uint16_t compression = field_defaulted<std::uint16_t>(tif, TIFFTAG_COMPRESSION, COMPRESSION_NONE);
    const std::uint16_t planar_config = field_defaulted<std::uint16_t>(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
    std::uint16_t orientation = field_defaulted<std::uint16_t>(tif, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
    std::uint16_t photometric = 0;
    if (!TIFFGetField(tif, TIFFTAG_PHOTOMETRIC, &photometric)) {
        photometric = spp >= 3 ? PHOTOMETRIC_RGB : PHOTOMETRIC_MINISBLACK;
        PLOGW << "TIFF: no PhotometricInterpretation, assuming " << photometric_name(photometric);
    }
    if (spp == 0 || spp > kMaxChannels)
        return make_error(UnsupportedFormatError, "TIFF: " + std::to_string(spp) + " samples per pixel are not supported");
    if (orientation < ORIENTATION_TOPLEFT || orientation > ORIENTATION_LEFTBOT)
        orientation = ORIENTATION_TOPLEFT;
    if (orientation > ORIENTATION_BOTLEFT)
        PLOGW << "TIFF: orientation " << orientation << " (transposed) is not supported, the image is shown as stored";
    this->orientation = orientation;

    // decoder output formats: JPEG YCbCr → RGB, LogLuv/LogL → float XYZ/Y
    this->jpeg_rgb = photometric == PHOTOMETRIC_YCBCR && compression == COMPRESSION_JPEG;
    this->sgilog_float = (photometric == PHOTOMETRIC_LOGLUV || photometric == PHOTOMETRIC_LOGL)
                      && (compression == COMPRESSION_SGILOG || compression == COMPRESSION_SGILOG24);
    if (this->jpeg_rgb && !TIFFSetField(tif, TIFFTAG_JPEGCOLORMODE, JPEGCOLORMODE_RGB))
        return make_error(ImageLoadError, h.take_error("couldn't set the JPEG color mode"));
    if (this->sgilog_float && !TIFFSetField(tif, TIFFTAG_SGILOGDATAFMT, SGILOGDATAFMT_FLOAT))
        return make_error(ImageLoadError, h.take_error("couldn't set the LogLuv data format"));

    const std::uint16_t bits = field_defaulted<std::uint16_t>(tif, TIFFTAG_BITSPERSAMPLE, 1);
    const std::uint16_t sample_format = field_defaulted<std::uint16_t>(tif, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_UINT);

    // --- color: which samples are colors, and how they become RGB or gray
    tiff::ConvertSpec& s = this->spec;
    s = {};
    s.width = width;
    s.bits = bits;
    s.samples = spp;
    s.planar = planar_config == PLANARCONFIG_SEPARATE && spp > 1;
    s.mirror = orientation == ORIENTATION_TOPRIGHT || orientation == ORIENTATION_BOTRIGHT;
    bool use_rgba = false;
    switch (photometric) {
    case PHOTOMETRIC_MINISBLACK:
    case PHOTOMETRIC_MASK:
    case PHOTOMETRIC_CFA: // raw sensor data, shown as gray
    case PHOTOMETRIC_LOGL: s.op = ColorOp::Copy, s.color_in = 1; break;
    case kPhotometricLinearRaw: s.op = ColorOp::Copy, s.color_in = spp >= 3 ? 3 : 1; break;
    case PHOTOMETRIC_MINISWHITE: s.op = ColorOp::MinIsWhite, s.color_in = 1; break;
    case PHOTOMETRIC_RGB: s.op = ColorOp::Copy, s.color_in = 3; break;
    case PHOTOMETRIC_YCBCR:
        if (this->jpeg_rgb)
            s.op = ColorOp::Copy, s.color_in = 3;
        else
            use_rgba = true;
        break;
    case PHOTOMETRIC_PALETTE: s.op = ColorOp::Palette, s.color_in = 1; break;
    case PHOTOMETRIC_SEPARATED:
        if (field_defaulted<std::uint16_t>(tif, TIFFTAG_INKSET, INKSET_CMYK) != INKSET_CMYK
            || field_defaulted<std::uint16_t>(tif, TIFFTAG_NUMBEROFINKS, 4) != 4)
            return make_error(UnsupportedFormatError, "TIFF: only CMYK separated images are supported");
        s.op = ColorOp::Cmyk, s.color_in = 4;
        break;
    case PHOTOMETRIC_LOGLUV:
        if (this->sgilog_float)
            s.op = ColorOp::XyzToRgb, s.color_in = 3;
        else
            use_rgba = true;
        break;
    default: use_rgba = true; break; // CIE Lab, ICC Lab, ITU Lab, ...
    }

    switch (sample_format) {
    case SAMPLEFORMAT_UINT:
    case SAMPLEFORMAT_VOID: s.format = InFormat::UInt; break;
    case SAMPLEFORMAT_INT: s.format = InFormat::Int; break;
    case SAMPLEFORMAT_IEEEFP: s.format = InFormat::Float; break;
    default: return make_error(UnsupportedFormatError, "TIFF: complex samples are not supported");
    }
    const bool bits_ok = s.format == InFormat::Float ? (bits == 16 || bits == 24 || bits == 32 || bits == 64)
                                                     : ((bits >= 1 && bits <= 32) || bits == 64);
    if (!bits_ok && !use_rgba)
        return make_error(UnsupportedFormatError, "TIFF: " + std::to_string(bits) + "-bit samples are not supported");
    if (!use_rgba && spp < s.color_in)
        return make_error(CorruptImageError, std::string("TIFF: too few samples per pixel for ") + photometric_name(photometric));
    if (s.op == ColorOp::Palette && bits > 16)
        return make_error(UnsupportedFormatError, "TIFF: palette images with more than 16 bits per index are not supported");

    // --- extra samples: the first alpha is A, the others are kept as Other
    std::uint16_t extra_count = 0;
    std::uint16_t* extra_types = nullptr;
    TIFFGetField(tif, TIFFTAG_EXTRASAMPLES, &extra_count, &extra_types);
    const std::uint8_t color_samples = use_rgba ? 0 : s.color_in;
    auto extra_type = [&](std::size_t sample) -> std::uint16_t {
        const std::size_t first_declared = spp - std::min<std::size_t>(extra_count, spp);
        return sample >= first_declared && extra_types ? extra_types[sample - first_declared] : EXTRASAMPLE_UNSPECIFIED;
    };
    for (std::size_t sample = color_samples; sample < spp && !use_rgba; ++sample) {
        const std::uint16_t type = extra_type(sample);
        if (s.alpha < 0 && (type == EXTRASAMPLE_ASSOCALPHA || type == EXTRASAMPLE_UNASSALPHA)) {
            s.alpha = static_cast<int>(sample);
            s.unpremultiply = type == EXTRASAMPLE_ASSOCALPHA;
        }
    }

    PixelLayout& layout = this->image_info.layout;
    layout = {};
    this->image_info.width = width;
    this->image_info.height = height;
    this->image_info.range.reset();
    layout.rows = orientation == ORIENTATION_BOTRIGHT || orientation == ORIENTATION_BOTLEFT ? RowOrder::BottomUp : RowOrder::TopDown;
    layout.big_endian = false; // libtiff decodes to host byte order

    this->tiled = TIFFIsTiled(tif);
    if (this->tiled) {
        std::uint32_t tw = 0, th = 0;
        TIFFGetField(tif, TIFFTAG_TILEWIDTH, &tw);
        TIFFGetField(tif, TIFFTAG_TILELENGTH, &th);
        if (tw == 0 || th == 0)
            return make_error(CorruptImageError, "TIFF: invalid tile size");
        this->unit_width = tw;
        this->unit_rows = th;
    } else {
        this->unit_width = width;
        this->unit_rows = std::clamp<std::uint32_t>(field_defaulted<std::uint32_t>(tif, TIFFTAG_ROWSPERSTRIP, height), 1, height);
    }

    if (use_rgba) {
        char message[1024] = "";
        if (!TIFFRGBAImageOK(tif, message))
            return make_error(UnsupportedFormatError, std::string("TIFF: ") + message);
        TIFFRGBAImage img{};
        if (!TIFFRGBAImageBegin(&img, tif, 1, message))
            return make_error(UnsupportedFormatError, std::string("TIFF: ") + message);
        this->rgba_alpha = img.alpha != 0;
        TIFFRGBAImageEnd(&img);

        this->path = Path::Rgba;
        this->scanlines = false;
        s.out_sample = SampleType::U8;
        s.out_channels = 4;
        layout.sample = SampleType::U8;
        layout.channels = 4;
        layout.roles = {ChannelRole::R, ChannelRole::G, ChannelRole::B, this->rgba_alpha ? ChannelRole::A : ChannelRole::Unused};
        layout.stride = std::size_t{width} * 4;
        // one strip for the whole image is decoded in one go
        this->streamable = this->tiled || this->unit_rows < height || height <= kBandRows;
    } else {
        // --- output samples
        if (s.op == ColorOp::Palette) {
            std::uint16_t *r = nullptr, *g = nullptr, *b = nullptr;
            if (!TIFFGetField(tif, TIFFTAG_COLORMAP, &r, &g, &b))
                return make_error(CorruptImageError, "TIFF: palette image without a colormap");
            const std::size_t entries = std::size_t{1} << bits;
            s.colormap = {std::vector<std::uint16_t>(r, r + entries), std::vector<std::uint16_t>(g, g + entries),
                          std::vector<std::uint16_t>(b, b + entries)};
            // some writers store 8-bit values in the 16-bit colormap (libtiff's TIFFRGBAImage detects them the same way)
            bool eight_bit = true, exact8 = true;
            for (const auto& channel : s.colormap)
                for (std::uint16_t v : channel) {
                    eight_bit = eight_bit && v < 256;
                    exact8 = exact8 && v % 257 == 0;
                }
            if (eight_bit) {
                for (auto& channel : s.colormap)
                    for (std::uint16_t& v : channel)
                        v = static_cast<std::uint16_t>(v * 257);
                exact8 = true;
            }
            s.out_sample = exact8 ? SampleType::U8 : SampleType::U16;
        } else if (this->sgilog_float || bits == 64 || (s.format == InFormat::Float && bits != 16)) {
            s.out_sample = SampleType::F32;
        } else if (s.format == InFormat::Float) {
            s.out_sample = SampleType::F16;
        } else {
            const bool sign = s.format == InFormat::Int;
            s.out_sample = bits <= 8 ? (sign ? SampleType::I8 : SampleType::U8)
                         : bits <= 16 ? (sign ? SampleType::I16 : SampleType::U16)
                                      : (sign ? SampleType::I32 : SampleType::U32);
        }
        const std::uint8_t color_out = s.color_out();
        const std::size_t channels = color_out + (spp - s.color_in);
        if (channels > kMaxChannels)
            return make_error(UnsupportedFormatError, "TIFF: too many channels");
        s.out_channels = static_cast<std::uint8_t>(channels);

        layout.sample = s.out_sample;
        layout.channels = s.out_channels;
        layout.roles.fill(ChannelRole::Other);
        if (color_out == 1) {
            layout.roles[0] = ChannelRole::Gray;
        } else {
            layout.roles[0] = ChannelRole::R;
            layout.roles[1] = ChannelRole::G;
            layout.roles[2] = ChannelRole::B;
        }
        if (s.alpha >= 0)
            layout.roles[color_out + (s.alpha - s.color_in)] = ChannelRole::A;
        layout.stride = std::size_t{width} * layout.bytes_per_pixel();

        // value range from the file, for float and 32-bit integer data (whose type range means nothing)
        double smin = 0, smax = 0;
        const bool wide = s.out_sample == SampleType::F16 || s.out_sample == SampleType::F32 || s.out_sample == SampleType::U32
                       || s.out_sample == SampleType::I32;
        if (wide && !this->sgilog_float && TIFFGetField(tif, TIFFTAG_SMINSAMPLEVALUE, &smin) && TIFFGetField(tif, TIFFTAG_SMAXSAMPLEVALUE, &smax)
            && smax > smin) {
            this->image_info.range = std::array<float, 2>{static_cast<float>(smin), static_cast<float>(smax)};
            s.float_range = *this->image_info.range;
        }

        const bool native_bits = (bits == 8 || bits == 16 || bits == 32) && sample_size(s.out_sample) * 8 == bits;
        const bool native = s.op == ColorOp::Copy && !s.unpremultiply && !s.mirror && !s.planar && native_bits;
        this->path = native ? Path::Native : Path::Convert;
        this->scanlines = !this->tiled && !s.planar && TIFFStripSize64(tif) > kScanlineStripBytes;
        this->streamable = this->tiled || this->scanlines || this->unit_rows < height || height <= kBandRows;

        // raw-mappable: uncompressed rows stored back to back, in the output layout but in the file's byte order
        if (native && compression == COMPRESSION_NONE && !this->tiled) {
            const std::uint64_t stride = layout.stride;
            const std::uint32_t strips = TIFFNumberOfStrips(tif);
            const std::uint64_t first = TIFFGetStrileOffset(tif, 0);
            bool contiguous = first != 0 && first + stride * height <= this->map.size();
            for (std::uint32_t i = 1; i < strips && contiguous; ++i)
                contiguous = TIFFGetStrileOffset(tif, i) == first + std::uint64_t{i} * this->unit_rows * stride;
            if (contiguous) {
                this->path = Path::Raw;
                this->raw_offset = static_cast<std::size_t>(first);
                layout.big_endian = TIFFIsBigEndian(tif) != 0;
                this->streamable = true;
            }
        }
    }
    this->raw = this->path == Path::Raw;

    const std::span<const std::uint8_t> file{reinterpret_cast<const std::uint8_t*>(this->map.data()), this->map.size()};
    this->header = header_and_ifd(file, TIFFCurrentDirOffset(tif), TIFFIsBigTIFF(tif) != 0, TIFFIsBigEndian(tif) != 0);

    const char* compression_name = "?";
    if (const TIFFCodec* codec = TIFFFindCODEC(compression))
        compression_name = codec->name;
    PLOGI << "TIFF: " << width << "x" << height << ", " << spp << " x " << bits << "-bit "
          << (s.format == InFormat::Float ? "float" : s.format == InFormat::Int ? "int" : "uint") << " "
          << photometric_name(photometric) << ", " << compression_name << ", "
          << (this->tiled ? "tiles " + std::to_string(this->unit_width) + "x" + std::to_string(this->unit_rows)
                          : std::to_string(this->unit_rows) + " rows per strip")
          << (s.planar ? ", planar" : "") << (TIFFIsBigTIFF(tif) ? ", BigTIFF" : "") << (TIFFIsBigEndian(tif) ? ", big-endian" : "")
          << " -> " << path_label(this->path);
    return {};
}

std::span<const std::uint8_t> TiffSource::mapped_pixels() {
    if (!this->raw || !this->map.is_mapped())
        return {};
    return {reinterpret_cast<const std::uint8_t*>(this->map.data()) + this->raw_offset, this->image_info.layout.stride * this->image_info.height};
}

std::expected<void, enim::Error> TiffSource::decode(RowSink& sink, std::stop_token stop) {
    switch (this->path) {
    case Path::Raw: {
        const std::span<const std::uint8_t> pixels = this->mapped_pixels();
        const std::size_t stride = this->image_info.layout.stride;
        const std::size_t height = this->image_info.height;
        for (std::size_t y = 0; y < height; y += kRawRowsPerChunk) {
            if (stop.stop_requested()) {
                auto error = make_error(DecodeCancelled, "decode cancelled");
                sink.on_done(error);
                return error;
            }
            sink.on_rows(y, std::min(kRawRowsPerChunk, height - y), pixels.data() + y * stride, stride);
        }
        sink.on_done({});
        return {};
    }
    case Path::Rgba: return this->decode_rgba(sink, stop);
    default: return this->scanlines ? this->decode_scanlines(sink, stop) : this->decode_units(sink, stop);
    }
}

namespace {

/// @brief Emits decoded rows: as they are (Native), or converted by the pool's workers into out.
struct Emitter {
    const tiff::ConvertSpec& spec;
    bool native;
    std::size_t row_bytes;  // decoded bytes per row and plane
    std::size_t out_stride;
    std::vector<std::uint8_t> out;

    void emit(RowSink& sink, ThreadPool& pool, std::size_t y0, std::size_t rows, const std::vector<std::vector<std::uint8_t>>& planes) {
        if (rows == 0)
            return;
        if (this->native) {
            sink.on_rows(y0, rows, planes[0].data(), this->row_bytes);
            return;
        }
        if (this->out.size() < rows * this->out_stride)
            this->out.resize(rows * this->out_stride);
        constexpr std::size_t kRowsPerItem = 8;
        pool.run((rows + kRowsPerItem - 1) / kRowsPerItem, [&](std::size_t item) {
            std::array<const std::uint8_t*, kMaxChannels> rows_in{};
            const std::size_t end = std::min(rows, (item + 1) * kRowsPerItem);
            for (std::size_t r = item * kRowsPerItem; r < end; ++r) {
                for (std::size_t p = 0; p < planes.size(); ++p)
                    rows_in[p] = planes[p].data() + r * this->row_bytes;
                tiff::convert_row(this->spec, rows_in.data(), this->out.data() + r * this->out_stride);
            }
        });
        sink.on_rows(y0, rows, this->out.data(), this->out_stride);
    }
};

std::expected<void, Error> finish(RowSink& sink, std::expected<void, Error> result) {
    sink.on_done(result);
    return result;
}

} // namespace

std::expected<void, enim::Error> TiffSource::decode_units(RowSink& sink, std::stop_token stop) {
    const tiff::ConvertSpec& s = this->spec;
    const std::size_t width = this->image_info.width;
    const std::size_t height = this->image_info.height;
    const std::size_t planes = s.planar ? s.samples : 1;
    const std::size_t row_bytes = tiff::decoded_row_bytes(s);
    const std::size_t units_x = this->tiled ? (width + this->unit_width - 1) / this->unit_width : 1;
    const std::size_t units_total_y = (height + this->unit_rows - 1) / this->unit_rows;

    // Rows per band: whole units, at least kBandRows, enough units to keep the workers busy, at most kBandBytes
    // (but always one unit row).
    const std::size_t threads = ThreadPool::default_workers() + 1;
    const std::size_t unit_bytes = std::size_t{this->unit_rows} * row_bytes * planes;
    std::size_t units_y = std::max((kBandRows + this->unit_rows - 1) / this->unit_rows, (threads + units_x * planes - 1) / (units_x * planes));
    units_y = std::clamp<std::size_t>(std::min(units_y, kBandBytes / std::max<std::size_t>(unit_bytes, 1)), 1, units_total_y);
    const std::size_t band_rows = units_y * this->unit_rows;
    const std::size_t items_per_band = units_y * units_x * planes;
    ThreadPool pool(std::min(threads, items_per_band) - 1);

    HandlePool handles{this->map, this->path_name, this->directory, this->jpeg_rgb, this->sgilog_float, {}, {}};
    if (auto first = handles.acquire())
        handles.release(std::move(*first));
    else
        return finish(sink, std::unexpected(first.error()));

    std::vector<std::vector<std::uint8_t>> raw(planes);
    for (auto& plane : raw) {
        plane.resize(band_rows * row_bytes + kPad);
    }
    Emitter emitter{s, this->path == Path::Native, row_bytes, this->image_info.layout.stride, {}};
    const std::size_t plane_samples = s.planar ? 1 : s.samples;

    std::vector<std::optional<Error>> errors(items_per_band);
    for (std::size_t y0 = 0; y0 < height; y0 += band_rows) {
        if (stop.stop_requested())
            return finish(sink, make_error(DecodeCancelled, "decode cancelled"));
        const std::size_t rows = std::min(band_rows, height - y0);
        const std::size_t band_units_y = (rows + this->unit_rows - 1) / this->unit_rows;
        const std::size_t items = band_units_y * units_x * planes;
        std::fill(errors.begin(), errors.end(), std::nullopt);

        pool.run(items, [&](std::size_t item) {
            const std::size_t plane = item % planes;
            const std::size_t ux = (item / planes) % units_x;
            const std::size_t uy = item / planes / units_x;
            const std::size_t unit_y0 = y0 + uy * this->unit_rows;
            const std::size_t unit_rows_here = std::min<std::size_t>(this->unit_rows, height - unit_y0);

            auto acquired = handles.acquire();
            if (!acquired) {
                errors[item] = acquired.error();
                return;
            }
            Handle& h = **acquired;
            const auto sample = static_cast<std::uint16_t>(plane);
            std::uint8_t* dst = raw[plane].data() + (unit_y0 - y0) * row_bytes;
            if (!this->tiled) {
                const std::uint32_t strip = TIFFComputeStrip(h.tif, static_cast<std::uint32_t>(unit_y0), sample);
                const tmsize_t want = static_cast<tmsize_t>(unit_rows_here * row_bytes);
                const tmsize_t got = TIFFReadEncodedStrip(h.tif, strip, dst, want);
                if (got < want)
                    errors[item] = Error{CorruptImageError, h.take_error("couldn't decode a strip")};
            } else {
                const tmsize_t tile_size = TIFFTileSize(h.tif);
                const std::size_t tile_row = static_cast<std::size_t>(TIFFTileRowSize64(h.tif));
                if (h.scratch.size() < static_cast<std::size_t>(tile_size))
                    h.scratch.resize(static_cast<std::size_t>(tile_size));
                const std::uint32_t tile = TIFFComputeTile(h.tif, static_cast<std::uint32_t>(ux * this->unit_width),
                                                           static_cast<std::uint32_t>(unit_y0), 0, sample);
                if (tile_size <= 0 || TIFFReadEncodedTile(h.tif, tile, h.scratch.data(), tile_size) < 0) {
                    errors[item] = Error{CorruptImageError, h.take_error("couldn't decode a tile")};
                } else {
                    // tile widths are multiples of 16, so every tile starts on a byte boundary
                    const std::size_t x_byte = ux * this->unit_width * plane_samples * s.bits / 8;
                    const std::size_t bytes = std::min(tile_row, row_bytes - x_byte);
                    for (std::size_t r = 0; r < unit_rows_here; ++r)
                        std::memcpy(dst + r * row_bytes + x_byte, h.scratch.data() + r * tile_row, bytes);
                }
            }
            handles.release(std::move(*acquired));
        });

        // the rows before the first failing unit row are complete
        std::optional<std::size_t> failed_uy;
        const Error* failure = nullptr;
        for (std::size_t item = 0; item < items; ++item)
            if (errors[item] && (!failed_uy || item / planes / units_x < *failed_uy)) {
                failed_uy = item / planes / units_x;
                failure = &*errors[item];
            }
        if (failure) {
            emitter.emit(sink, pool, y0, *failed_uy * this->unit_rows, raw);
            return finish(sink, std::unexpected(*failure));
        }
        emitter.emit(sink, pool, y0, rows, raw);
    }
    return finish(sink, {});
}

std::expected<void, enim::Error> TiffSource::decode_scanlines(RowSink& sink, std::stop_token stop) {
    const std::size_t height = this->image_info.height;
    const std::size_t row_bytes = tiff::decoded_row_bytes(this->spec);

    auto opened = open_handle(this->map, this->path_name, this->directory, this->jpeg_rgb, this->sgilog_float);
    if (!opened)
        return finish(sink, std::unexpected(opened.error()));
    Handle& h = **opened;

    ThreadPool pool(std::min<std::size_t>(ThreadPool::default_workers(), kBandRows / 8));
    std::vector<std::vector<std::uint8_t>> raw(1, std::vector<std::uint8_t>(kBandRows * row_bytes + kPad));
    Emitter emitter{this->spec, this->path == Path::Native, row_bytes, this->image_info.layout.stride, {}};
    for (std::size_t y0 = 0; y0 < height; y0 += kBandRows) {
        if (stop.stop_requested())
            return finish(sink, make_error(DecodeCancelled, "decode cancelled"));
        const std::size_t rows = std::min(kBandRows, height - y0);
        for (std::size_t r = 0; r < rows; ++r) {
            if (TIFFReadScanline(h.tif, raw[0].data() + r * row_bytes, static_cast<std::uint32_t>(y0 + r), 0) < 0) {
                emitter.emit(sink, pool, y0, r, raw);
                return finish(sink, make_error(CorruptImageError, h.take_error("couldn't decode a row")));
            }
        }
        emitter.emit(sink, pool, y0, rows, raw);
    }
    return finish(sink, {});
}

std::expected<void, enim::Error> TiffSource::decode_rgba(RowSink& sink, std::stop_token stop) {
    const std::size_t width = this->image_info.width;
    const std::size_t height = this->image_info.height;

    auto opened = open_handle(this->map, this->path_name, this->directory, false, false);
    if (!opened)
        return finish(sink, std::unexpected(opened.error()));
    Handle& h = **opened;

    char message[1024] = "";
    TIFFRGBAImage img{};
    if (!TIFFRGBAImageBegin(&img, h.tif, 1, message))
        return finish(sink, make_error(UnsupportedFormatError, std::string("TIFF: ") + message));
    struct End {
        TIFFRGBAImage* img;
        ~End() { TIFFRGBAImageEnd(this->img); }
    } end{&img};
    // the requested orientation is the file's: libtiff flips nothing, rows come in storage order like the other paths
    img.req_orientation = this->orientation;

    // whole units (TIFFRGBAImageGet decodes every strip or tile it touches); one strip for the image: all at once
    std::size_t band_rows = this->unit_rows * ((kBandRows + this->unit_rows - 1) / this->unit_rows);
    band_rows = std::min(band_rows, height);
    std::vector<std::uint8_t> raster(band_rows * width * 4);

    for (std::size_t y0 = 0; y0 < height; y0 += band_rows) {
        if (stop.stop_requested())
            return finish(sink, make_error(DecodeCancelled, "decode cancelled"));
        const std::size_t rows = std::min(band_rows, height - y0);
        img.row_offset = static_cast<int>(y0);
        img.col_offset = 0;
        if (!TIFFRGBAImageGet(&img, reinterpret_cast<std::uint32_t*>(raster.data()), static_cast<std::uint32_t>(width),
                              static_cast<std::uint32_t>(rows)))
            return finish(sink, make_error(CorruptImageError, h.take_error("couldn't decode the image")));
        for (std::size_t r = 0; r < rows; ++r)
            tiff::finish_rgba_row(raster.data() + r * width * 4, width, this->rgba_alpha, this->spec.mirror);
        sink.on_rows(y0, rows, raster.data(), width * 4);
    }
    return finish(sink, {});
}

} // namespace enim
