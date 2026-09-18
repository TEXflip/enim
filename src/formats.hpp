#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <unordered_map>

namespace enim {

struct FileHeader {
    virtual std::size_t get_size() const = 0;
    virtual std::array<std::size_t, 4> get_shape() const = 0; // B, H, W, C
};

struct FileSignature {
    std::array<std::uint8_t, 16> bytes{};
    std::size_t size{};
    std::size_t offset{};
};

struct FileFormat {
    std::string_view name;
    std::string_view mime_type;
    FileSignature signature;
};

constexpr FileFormat FORMATS[] = {
    // --- Raster formats ---
    {"PNG", "image/png", {{0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A}, 8, 0}},
    {"JPEG", "image/jpeg", {{0xFF, 0xD8, 0xFF}, 3, 0}},
    {"GIF87a", "image/gif", {{'G', 'I', 'F', '8', '7', 'a'}, 6, 0}},
    {"GIF89a", "image/gif", {{'G', 'I', 'F', '8', '9', 'a'}, 6, 0}},
    {"BMP", "image/bmp", {{'B', 'M'}, 2, 0}},
    {"TIFF (little-endian)", "image/tiff", {{0x49, 0x49, 0x2A, 0x00}, 4, 0}},
    {"TIFF (big-endian)", "image/tiff", {{0x4D, 0x4D, 0x00, 0x2A}, 4, 0}},

    // WEBP: bytes 0-3 are "RIFF", bytes 4-7 are file size (variable),
    // so the only fixed, size-independent signature is "WEBP" at offset 8.
    {"WEBP", "image/webp", {{'W', 'E', 'B', 'P'}, 4, 8}},

    {"ICO", "image/x-icon", {{0x00, 0x00, 0x01, 0x00}, 4, 0}},
    {"CUR", "image/x-icon", {{0x00, 0x00, 0x02, 0x00}, 4, 0}},
    {"PSD", "image/vnd.adobe.photoshop", {{'8', 'B', 'P', 'S'}, 4, 0}},

    // ISO-BMFF based formats: bytes 4-7 are always "ftyp"; the brand
    // identifier that distinguishes the specific format sits at offset 8.
    {"HEIC", "image/heic", {{'h', 'e', 'i', 'c'}, 4, 8}},
    {"HEIF (mif1)", "image/heif", {{'m', 'i', 'f', '1'}, 4, 8}},
    {"AVIF", "image/avif", {{'a', 'v', 'i', 'f'}, 4, 8}},

    {"JPEG 2000 (JP2)", "image/jp2", {{0x00, 0x00, 0x00, 0x0C, 0x6A, 0x50, 0x20, 0x20, 0x0D, 0x0A, 0x87, 0x0A}, 12, 0}},

    {"DDS", "image/vnd.ms-dds", {{'D', 'D', 'S', ' '}, 4, 0}},
    {"OpenEXR", "image/x-exr", {{0x76, 0x2F, 0x31, 0x01}, 4, 0}},
    {"GIMP XCF", "image/x-xcf", {{'g', 'i', 'm', 'p', ' ', 'x', 'c', 'f', ' '}, 9, 0}},
    {"JPEG XR (HD Photo)", "image/vnd.ms-photo", {{0x49, 0x49, 0xBC, 0x01}, 4, 0}},

    // JPEG XL has two possible signatures: bare codestream or ISOBMFF container.
    {"JPEG XL (codestream)", "image/jxl", {{0xFF, 0x0A}, 2, 0}},
    {"JPEG XL (container)", "image/jxl", {{0x00, 0x00, 0x00, 0x0C, 0x4A, 0x58, 0x4C, 0x20, 0x0D, 0x0A, 0x87, 0x0A}, 12, 0}},

    {"FLIF", "image/flif", {{'F', 'L', 'I', 'F'}, 4, 0}},
    {"ICNS", "image/icns", {{'i', 'c', 'n', 's'}, 4, 0}},

    {"KTX 1.1", "image/ktx", {{0xAB, 0x4B, 0x54, 0x58, 0x20, 0x31, 0x31, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A}, 12, 0}},
    {"KTX2", "image/ktx2", {{0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32, 0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A}, 12, 0}},

    {"QOI (Quite OK Image)", "image/qoi", {{'q', 'o', 'i', 'f'}, 4, 0}},

    {"WMF (placeable)", "image/wmf", {{0xD7, 0xCD, 0xC6, 0x9A}, 4, 0}},

    // NetPBM family: signature is just "P" + a digit (1-6); ambiguous with
    // arbitrary text starting the same way, but this is the accepted magic.
    {"PBM (ASCII)", "image/x-portable-bitmap", {{'P', '1'}, 2, 0}},
    {"PGM (ASCII)", "image/x-portable-graymap", {{'P', '2'}, 2, 0}},
    {"PPM (ASCII)", "image/x-portable-pixmap", {{'P', '3'}, 2, 0}},
    {"PBM (binary)", "image/x-portable-bitmap", {{'P', '4'}, 2, 0}},
    {"PGM (binary)", "image/x-portable-graymap", {{'P', '5'}, 2, 0}},
    {"PPM (binary)", "image/x-portable-pixmap", {{'P', '6'}, 2, 0}},
};

#define COMPUTE_MAX_READ_SIZE(FORMATS_ARRAY)                                                                                                         \
    []() -> std::size_t {                                                                                                                            \
        std::size_t max_size = 0;                                                                                                                    \
        std::size_t max_offset = 0;                                                                                                                  \
        for (const auto& f : FORMATS_ARRAY) {                                                                                                        \
            if (f.signature.size > max_size)                                                                                                         \
                max_size = f.signature.size;                                                                                                         \
            if (f.signature.offset > max_offset)                                                                                                     \
                max_offset = f.signature.offset;                                                                                                     \
        }                                                                                                                                            \
        return max_size + max_offset;                                                                                                                \
    }()

// Compute the maximum number of bytes to read for any image format signature at compile time.
constexpr std::size_t kMaxSignatureReadSize = COMPUTE_MAX_READ_SIZE(FORMATS);

} // namespace enim