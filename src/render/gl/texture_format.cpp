#include "texture_format.hpp"

#include <bit>

namespace {

using enim::SampleType;

// [sample][channels - 1]
constexpr GLenum kInternalFormats[][4] = {
    /* U8  */ {GL_R8, GL_RG8, GL_RGB8, GL_RGBA8},
    /* U16 */ {GL_R16, GL_RG16, GL_RGB16, GL_RGBA16},
    /* I8  */ {GL_R8_SNORM, GL_RG8_SNORM, GL_RGB8_SNORM, GL_RGBA8_SNORM},
    /* I16 */ {GL_R16_SNORM, GL_RG16_SNORM, GL_RGB16_SNORM, GL_RGBA16_SNORM},
    /* F16 */ {GL_R16F, GL_RG16F, GL_RGB16F, GL_RGBA16F},
    /* F32 */ {GL_R32F, GL_RG32F, GL_RGB32F, GL_RGBA32F},
};

std::size_t format_row(SampleType sample) {
    switch (sample) {
    case SampleType::U8: return 0;
    case SampleType::U16: return 1;
    case SampleType::I8: return 2;
    case SampleType::I16: return 3;
    case SampleType::F16: return 4;
    default: return 5;
    }
}

/// @brief Texture sample type for a source sample type: 32-bit integers are not filterable, so they become float.
SampleType texture_sample(SampleType sample) {
    if (sample == SampleType::U32 || sample == SampleType::I32)
        return SampleType::F32;
    return sample;
}

std::optional<GLenum> gl_type(SampleType sample) {
    switch (sample) {
    case SampleType::U8: return GL_UNSIGNED_BYTE;
    case SampleType::U16: return GL_UNSIGNED_SHORT;
    case SampleType::I8: return GL_BYTE;
    case SampleType::I16: return GL_SHORT;
    case SampleType::F16: return GL_HALF_FLOAT;
    case SampleType::F32: return GL_FLOAT;
    default: return std::nullopt;
    }
}

std::size_t align_up(std::size_t value, std::size_t alignment) { return (value + alignment - 1) / alignment * alignment; }

/// @brief The source channel the display wants in each texture channel.
std::array<std::int8_t, 4> wanted_channels(const enim::TextureFormat& tex, const enim::DisplayParams& display) {
    const auto& map = display.channel_map;
    switch (tex.channels) {
    case 1: return {map[0], -1, -1, -1};
    case 2: return {map[0], map[3], -1, -1};
    case 3: return {map[0], map[1], map[2], -1};
    default: return {map[0], map[1], map[2], map[3]};
    }
}

/// @brief GL pixel format reading the stored channels so that texture channel k gets the wanted source channel.
std::optional<GLenum> pixel_format(std::uint8_t stored, const std::array<std::int8_t, 4>& wanted, std::uint8_t tex_channels) {
    constexpr GLenum rgba_formats[] = {GL_RED, GL_RG, GL_RGB, GL_RGBA};
    constexpr GLenum bgra_formats[] = {0, 0, GL_BGR, GL_BGRA};
    // stored channel that ends up in texture channel k
    constexpr std::int8_t rgba_order[] = {0, 1, 2, 3};
    constexpr std::int8_t bgra_order[] = {2, 1, 0, 3};

    auto matches = [&](const std::int8_t* order) {
        for (std::uint8_t k = 0; k < tex_channels; ++k)
            if (wanted[k] != order[k])
                return false;
        return true;
    };

    if (matches(rgba_order))
        return rgba_formats[stored - 1];
    if (stored >= 3 && matches(bgra_order))
        return bgra_formats[stored - 1];
    return std::nullopt;
}

} // namespace

namespace enim {

TextureFormat choose_texture_format(const PixelLayout& layout, const DisplayParams& display) {
    TextureFormat tex;
    tex.sample = texture_sample(layout.sample);

    bool mono = display.is_mono();
    bool alpha = display.has_alpha();
    if (mono) {
        tex.channels = alpha ? 2 : 1;
        tex.swizzle = {GL_RED, GL_RED, GL_RED, alpha ? GL_GREEN : GL_ONE};
    } else {
        tex.channels = alpha ? 4 : 3;
        tex.swizzle = {GL_RED, GL_GREEN, GL_BLUE, alpha ? GL_ALPHA : GL_ONE};
    }

    tex.internal_format = kInternalFormats[format_row(tex.sample)][tex.channels - 1];
    return tex;
}

TextureFormat renderable(const TextureFormat& format) {
    if (format.channels != 3)
        return format;
    TextureFormat out = format;
    out.channels = 4;
    out.internal_format = kInternalFormats[format_row(format.sample)][3];
    out.swizzle = {GL_RED, GL_GREEN, GL_BLUE, GL_ONE};
    return out;
}

std::optional<UploadParams> direct_upload_params(const PixelLayout& layout, std::size_t width, const TextureFormat& tex,
                                                 const DisplayParams& display) {
    if (layout.channels == 0 || layout.channels > 4 || tex.sample != layout.sample)
        return std::nullopt;
    // range normalization is only applied by a conversion, the hardware uses the type's full range
    if (!(display.range == default_range(layout.sample)))
        return std::nullopt;

    std::optional<GLenum> type = gl_type(layout.sample);
    if (!type)
        return std::nullopt;

    std::optional<GLenum> format = pixel_format(layout.channels, wanted_channels(tex, display), tex.channels);
    if (!format)
        return std::nullopt;

    UploadParams params;
    params.format = *format;
    params.type = *type;
    params.swap_bytes = sample_size(layout.sample) > 1 && layout.big_endian != (std::endian::native == std::endian::big);

    const std::size_t bpp = layout.bytes_per_pixel();
    const std::size_t row_bytes = width * bpp;
    params.alignment = 0;
    for (std::size_t a : {1, 2, 4, 8}) {
        if (layout.stride == align_up(row_bytes, a)) {
            params.alignment = static_cast<GLint>(a);
            break;
        }
    }
    if (params.alignment == 0) {
        if (layout.stride % bpp != 0)
            return std::nullopt;
        params.alignment = 1;
        params.row_length = static_cast<GLint>(layout.stride / bpp);
    }

    return params;
}

} // namespace enim
