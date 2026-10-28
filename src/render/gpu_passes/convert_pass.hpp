#pragma once

#include <cstddef>
#include <expected>

#include <glad/glad.h>

#include <errors.hpp>
#include <io/pixel_layout.hpp>
#include <linalgebra.hpp>
#include <render/gl/framebuffer.hpp>
#include <render/gl/raw_buffer.hpp>
#include <render/gl/texture.hpp>
#include <view/display_params.hpp>

namespace enim {

/// @brief Produces the display texture on the GPU: channel selection, depth conversion and range, written into an FBO.
/// One shader body (include/convert.glsl) with an input variant per source: raw bytes (GpuConvert) or a decoded
/// texture (GpuDecode, not implemented yet). Runs once at load time, never while rendering.
struct ConvertPass {
    ConvertPass() = default;
    ConvertPass(const ConvertPass&) = delete;
    ConvertPass& operator=(const ConvertPass&) = delete;
    ~ConvertPass();

    /// @brief Allocates the target texture (cleared to transparent) and its FBO, and builds the raw input program.
    /// @param dst A color-renderable format (see renderable()).
    std::expected<void, enim::Error> init_raw(const TextureFormat& dst, Vec2i size, const DisplayParams& display);

    /// @brief Converts a band of stored rows held in band, starting at storage row first_row, into the target.
    /// Leaves the default framebuffer bound and restores the viewport.
    void from_raw_band(const RawBuffer& band, const PixelLayout& src, std::size_t first_row, std::size_t rows);

    /// @brief The target texture while it is being filled (level 0 only), to draw progressively.
    const Texture& target() const { return this->texture; }

    /// @brief Releases the FBO and the program, and hands over the display texture (level 0 only).
    Texture finish();

  private:
    Texture texture;
    Framebuffer fbo;
    DisplayParams display;
    GLuint program = 0;
    GLuint vao = 0; // empty: the fullscreen triangle comes from gl_VertexID
};

} // namespace enim
