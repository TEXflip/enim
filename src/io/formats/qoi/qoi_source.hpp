#pragma once

#include <mio.hpp>

#include <io/image_source.hpp>

namespace enim {

/// @brief QOI (Quite OK Image, qoiformat.org): 8-bit RGB or RGBA, compressed, decoded on the CPU top-to-bottom.
struct QoiSource : public ImageSource {
    std::expected<void, enim::Error> open(const std::filesystem::path& path) override;
    const ImageInfo& info() const override { return this->image_info; }
    FormatCaps caps() const override { return {.raw_mappable = false, .gpu_decodable = false, .streamable = true}; }
    std::expected<void, enim::Error> decode(RowSink& sink, std::stop_token stop) override;

  private:
    mio::mmap_source map; // read-only mapping
    ImageInfo image_info;
};

} // namespace enim
