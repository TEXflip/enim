#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <stop_token>

#include <errors.hpp>
#include <io/pixel_layout.hpp>
#include <io/row_sink.hpp>

namespace enim {

struct FormatCaps {
    bool raw_mappable = false;  // pixels are stored uncompressed and can be read straight from the mmap
    bool gpu_decodable = false; // block-compressed: native GL support or a decode shader exists
    bool streamable = false;    // can decode top-to-bottom in row chunks
};

/// @brief Decodes an image file and emits its rows in the native layout.
struct ImageSource {
    virtual ~ImageSource() = default;

    /// @brief Reads the header. This is the first function called, before any other.
    virtual std::expected<void, enim::Error> open(const std::filesystem::path& path) = 0;
    virtual const ImageInfo& info() const = 0;
    virtual FormatCaps caps() const = 0;

    /// @brief The pixel data as stored in the file (raw_mappable only), info().height rows of info().layout.stride bytes.
    virtual std::span<const std::uint8_t> mapped_pixels() { return {}; }
    /// @brief The encoded pixel data (gpu_decodable only).
    virtual std::span<const std::uint8_t> encoded_bytes() { return {}; }

    virtual std::expected<void, enim::Error> decode(RowSink& sink, std::stop_token stop) = 0;
};

} // namespace enim
