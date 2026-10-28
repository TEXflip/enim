#pragma once

#include <cstddef>
#include <cstdint>

#include <io/image_source.hpp>
#include <render/gl/texture_format.hpp>
#include <settings/settings.hpp>
#include <view/display_params.hpp>

namespace enim {

enum class Mode { Fast, Normal };

enum class FastStrategy {
    DirectUpload, // uncompressed, GL-uploadable layout: glTexImage2D straight from the mmap
    GpuConvert,   // uncompressed, other layout: mmap bands -> buffer texture -> convert pass -> FBO texture
    GpuDecode,    // encoded upload -> decode pass -> optional convert pass -> texture
    StreamUpload, // CPU decoder -> CPU convert -> glTexSubImage2D, progressive
};

enum class Level0Source {
    SourceMmap,   // uncompressed: the source file already is level 0, no copy
    DecodeToDisk, // encoded: decode into a raw level-0 file in the cache
};

struct GpuLimits {
    std::size_t max_texture_size = 0;
    std::size_t max_texture_buffer_size = 0; // texels
};

constexpr std::size_t kGpuConvertStagingBytes = std::size_t{64} << 20;

struct LoadPlan {
    Mode mode = Mode::Fast;
    FastStrategy fast = FastStrategy::DirectUpload; // valid if mode == Fast
    Level0Source level0 = Level0Source::SourceMmap; // valid if mode == Normal
    DisplayParams display;
    TextureFormat texture;                          // display texture (fast) or frame texture (normal)
    std::size_t fast_bytes = 0;                     // GPU memory fast mode needs: texture + mipmaps + transient
    std::size_t band_bytes = 0;                     // GpuConvert: staging buffer budget for one band of rows
};

const char* to_string(Mode mode);
const char* to_string(FastStrategy strategy);

/// @brief Bytes of one GpuConvert band: the staging budget, capped by the buffer texture limit.
std::size_t gpu_convert_band_bytes(const GpuLimits& gpu);

/// @brief Decides mode, strategy and texture format. Pure: no GL or file access.
LoadPlan make_plan(const ImageInfo& info, const FormatCaps& caps, const GpuLimits& gpu, const Settings& settings);

} // namespace enim
