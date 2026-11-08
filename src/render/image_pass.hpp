#pragma once

#include <expected>

#include <glad/glad.h>

#include <errors.hpp>
#include <render/gl/texture.hpp>
#include <view/view_transform.hpp>

namespace enim {

/// @brief The final pass shared by every renderer: a quad mapped through the ViewTransform, sampling a display texture.
struct ImagePass {
    GLuint program = 0;
    GLuint vao = 0;
    GLuint vbo = 0;

    ImagePass() = default;
    ImagePass(const ImagePass&) = delete;
    ImagePass& operator=(const ImagePass&) = delete;
    ~ImagePass();

    /// @brief Creates the program and the quad. The GL context must be current.
    std::expected<void, enim::Error> init();

    /// @brief Draws the texture under the view, alpha-blended over the current framebuffer.
    /// @param flip_y True if the texture rows are stored bottom-up.
    void draw(const Texture& texture, const ViewTransform& view, bool flip_y) const;

    /// @brief Draws a tile ring (normal mode): pixel (x, y) of a pyramid level is texel (x mod width, y mod height) of
    /// the texture, which must wrap with GL_REPEAT. Colors are premultiplied by alpha, so filtering blends transparent
    /// texels correctly (straight alpha darkens the edge between opaque texels and transparent black ones).
    /// @param window_min, window_max The level pixels the ring holds, clamped to the level. Nothing is drawn outside
    /// them, and filtering never reads past them (like GL_CLAMP_TO_EDGE at the window's edges).
    void draw_ring(const Texture& texture, const ViewTransform& view, int level, Vec2f window_min, Vec2f window_max) const;

  private:
    struct TexelMapping {
        Vec2f scale, offset; // image coordinates in [0, 1] -> texel coordinates
        Vec2f min, max;      // texel clamp
    };
    void draw_mapped(const Texture& texture, const ViewTransform& view, bool flip_y, Vec2f region_min, Vec2f region_max,
                     bool premultiplied, const TexelMapping& mapping) const;
};

} // namespace enim
