#pragma once

#include <cstddef>

#include <pyramid/pyramid_format.hpp>
#include <view/view_transform.hpp>

namespace enim {

/// @brief Rectangle of pixels of one pyramid level, [x0, x1) x [y0, y1).
struct LevelRect {
    std::size_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;

    std::size_t width() const { return this->x1 - this->x0; }
    std::size_t height() const { return this->y1 - this->y0; }
    bool empty() const { return this->x1 <= this->x0 || this->y1 <= this->y0; }
    bool contains(const LevelRect& r) const {
        return r.x0 >= this->x0 && r.y0 >= this->y0 && r.x1 <= this->x1 && r.y1 <= this->y1;
    }
};

/// @brief What a normal mode frame shows: one texel per pixel of `level`, over `rect`.
struct FramePlan {
    int level = 0;
    LevelRect rect;

    /// @brief The image region the frame covers, in level-0 px. The last texels may reach past the image edge: the
    /// image shader discards what is outside the image.
    float image_min_x() const { return static_cast<float>(this->rect.x0 << this->level); }
    float image_min_y() const { return static_cast<float>(this->rect.y0 << this->level); }
    float image_max_x() const { return static_cast<float>(this->rect.x1 << this->level); }
    float image_max_y() const { return static_cast<float>(this->rect.y1 << this->level); }
};

/// @brief Pyramid level for a zoom: one screen px covers 1 to 2 px of it (less than 1 when zoomed in past 100%).
int choose_level(float zoom, int level_count);

/// @brief The pixels of a level visible in the view, clamped to the level.
LevelRect visible_rect(const ViewTransform& view, const PyramidGeometry& geom, int level);

/// @brief The frame for a view: the visible pixels of the chosen level plus a margin, so that small pans don't need a
/// new frame. At most max_texture_size texels per side.
FramePlan plan_frame(const ViewTransform& view, const PyramidGeometry& geom, std::size_t max_texture_size);

/// @brief True if the frame still serves the view: same level, and everything visible is inside it.
bool covers(const FramePlan& frame, const ViewTransform& view, const PyramidGeometry& geom);

} // namespace enim
