#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include <glad/glad.h>

#include <io/pixel_layout.hpp>
#include <view/display_params.hpp>

namespace enim {

/// @brief The format of a display-ready texture, the only kind of texture the image shader samples.
struct TextureFormat {
    GLenum internal_format = GL_RGBA8; // GL_R8, GL_RG16, GL_RGB16F, GL_RGBA32F, ...
    std::uint8_t channels = 4;         // 1..4: displayed channels only
    std::array<GLint, 4> swizzle{GL_RED, GL_GREEN, GL_BLUE, GL_ALPHA}; // GL_TEXTURE_SWIZZLE_RGBA
    SampleType sample = SampleType::U8; // depth after conversion

    std::size_t bytes_per_pixel() const { return sample_size(sample) * channels; }
};

/// @brief Picks the display texture format: displayed channels only, native depth when GL can filter it.
TextureFormat choose_texture_format(const PixelLayout& layout, const DisplayParams& display);

/// @brief The same format with 3 channels promoted to 4, for FBO targets (RGB is not guaranteed color-renderable).
TextureFormat renderable(const TextureFormat& format);

/// @brief Client-side parameters for uploading source rows with glTexImage2D.
struct UploadParams {
    GLenum format = GL_RGBA;
    GLenum type = GL_UNSIGNED_BYTE;
    GLint alignment = 4;  // GL_UNPACK_ALIGNMENT
    GLint row_length = 0; // GL_UNPACK_ROW_LENGTH, 0 = width
    bool swap_bytes = false;
};

/// @brief How to upload the source straight from its storage into a texture of format tex, if GL can read the layout as is.
/// @return The upload parameters, or nullopt if the source needs a conversion.
std::optional<UploadParams> direct_upload_params(const PixelLayout& layout, std::size_t width, const TextureFormat& tex,
                                                 const DisplayParams& display);

} // namespace enim
