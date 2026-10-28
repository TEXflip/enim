#pragma once

#include <expected>

#include <errors.hpp>
#include <linalgebra.hpp>
#include <view/view_transform.hpp>

namespace enim {

struct ImageRenderer {
    virtual ~ImageRenderer() = default;

    /// @brief Creates the GL resources needed to draw the image. The GL context must be current.
    /// @return An expected indicating success or containing an error.
    virtual std::expected<void, enim::Error> init() = 0;

    /// @brief Called before draw() with the current view, to prepare what the view needs.
    virtual void update(const ViewTransform& view) = 0;

    /// @brief Draws the image for the given view into the current framebuffer.
    virtual void draw(const ViewTransform& view) = 0;

    /// @brief True while the image is still being loaded: the window keeps redrawing, and update() advances the load.
    virtual bool loading() const { return false; }

    /// @brief Switches mipmapped minification on or off (fast mode only).
    /// @return True if the renderer supports mipmaps and a redraw is needed.
    virtual bool toggle_mipmaps() { return false; }

    /// @brief Size of the full resolution image, in image px.
    virtual Vec2f image_size() const = 0;
};

} // namespace enim
