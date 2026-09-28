#include "formats.hpp"
#include <mio.hpp>

namespace enim {
struct BmpReader : public enim::FileReader {

#pragma pack(push, 1)
    struct BmpHeader {
        std::array<char, 2> signature;
        std::uint32_t file_size;
        std::uint32_t reserved;
        std::uint32_t file_offset_to_pixel_array;

        // DIB header V3 fields
        std::uint32_t dib_header_size;
        std::uint32_t width;
        std::uint32_t height;
        std::uint16_t planes;
        std::uint16_t bits_per_pixel;
        std::uint32_t compression_method;
        std::uint32_t image_size;
        std::uint32_t W_px_per_meter;
        std::uint32_t H_px_per_meter;
        std::uint32_t colors_in_palette;
        std::uint32_t important_colors;
    };
#pragma pack(pop)

    static_assert(sizeof(BmpHeader) == 54);

    mio::mmap_source map; // read-only mapping
    BmpHeader header = {0};

    BmpReader() = default;
    ~BmpReader() {
        if (map.is_mapped()) {
            map.unmap();
        }
    }

    std::expected<void, enim::Error> load_header(const std::filesystem::path& path) override {
        std::error_code ec;
        map.map(path.string(), ec);
        if (ec) {
            return std::unexpected(enim::Error{enim::ErrorCode::FileOpenError, ec.message()});
        }
        if (map.size() < sizeof(header)) {
            return std::unexpected(enim::Error{enim::ErrorCode::FileOpenError, "BMP file too small"});
        }
        std::memcpy(&header, map.data(), sizeof(header));
        
        // Validate header
        if (header.signature[0] != 'B' || header.signature[1] != 'M') {
            return std::unexpected(enim::Error{enim::ErrorCode::FileOpenError, "Invalid BMP signature"});
        }
        if (header.width == 0 || header.height == 0) {
            return std::unexpected(enim::Error{enim::ErrorCode::FileOpenError, "Invalid BMP dimensions"});
        }
        if (header.bits_per_pixel != 24 && header.bits_per_pixel != 32) {
            return std::unexpected(enim::Error{enim::ErrorCode::FileOpenError, "Only 24-bit and 32-bit BMPs are supported"});
        }
        return {};
    }

    bool is_streamable() const override { return true; }

    bool is_tilable() const override { return true; }

    bool is_compressed() const override { return header.compression_method != 0; }

    const std::uint8_t* get_buffer_mmap() override {
        if (!map.is_mapped()) {
            return nullptr;
        }
        if (header.file_offset_to_pixel_array >= map.size()) {
            return nullptr;
        }
        return (std::uint8_t*) map.data() + header.file_offset_to_pixel_array;
    }

    std::size_t get_size() const override { 
        // BMP rows are padded to 4-byte boundary
        std::uint32_t bytes_per_pixel = header.bits_per_pixel / 8;
        std::uint32_t row_size = ((header.width * header.bits_per_pixel + 31) / 32) * 4;
        return header.height * row_size;
    }

    Shape get_shape() const override {
        return Shape{1, header.height, header.width, header.bits_per_pixel / 8}; // B, H, W, C (output will be RGBA)
    }
};

} // namespace enim