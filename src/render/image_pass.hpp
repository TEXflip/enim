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

    /// @brief Draws a texture covering only part of the image (a normal mode frame).
    /// @param region_min, region_max The image region the texture covers, in image px. Nothing is drawn outside it.
    /// @param premultiplied True if the texture's colors are premultiplied by alpha. Filtering then blends transparent
    /// texels correctly (straight alpha darkens the edge between opaque texels and transparent black ones).
    void draw(const Texture& texture, const ViewTransform& view, bool flip_y, Vec2f region_min, Vec2f region_max,
              bool premultiplied = false) const;
};

} // namespace enim
