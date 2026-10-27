#pragma once

#include <array>
#include <cstdint>

#include <io/pixel_layout.hpp>
#include <linalgebra.hpp>

namespace enim {

/// @brief What to display from the source channels. Applied when the display texture is produced, never in the image shader.
struct DisplayParams {
    std::array<std::int8_t, 4> channel_map{-1, -1, -1, -1}; // source channel per RGBA output, -1 = constant
    std::array<float, 4> constant{0, 0, 0, 1};
    Vec2f range{0, 1};                                      // value -> [0, 1]
    // later: per-channel range, gamma, colormap, ...

    bool is_mono() const { return channel_map[0] == channel_map[1] && channel_map[1] == channel_map[2]; }
    bool has_alpha() const { return channel_map[3] >= 0; }
};

/// @brief The range the hardware uses to normalize a sample type (UNORM/SNORM), or [0, 1] for float.
Vec2f default_range(SampleType sample);

/// @brief Default display from the channel roles: RGB(A) if present, else gray(+A), else the first channel as gray.
DisplayParams default_display(const PixelLayout& layout);

} // namespace enim
