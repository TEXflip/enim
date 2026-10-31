#include "tile_selection.hpp"

#include <algorithm>
#include <cmath>

namespace {

constexpr double kMarginFraction = 0.25; // of the visible size, on each side

} // namespace

namespace enim {

int choose_level(float zoom, int level_count) {
    if (zoom <= 0.0f)
        return level_count - 1;
    int level = static_cast<int>(std::floor(std::log2(1.0 / zoom)));
    return std::clamp(level, 0, level_count - 1);
}

LevelRect visible_rect(const ViewTransform& view, const PyramidGeometry& geom, int level) {
    const PyramidLevel& l = geom.levels[level];
    const double scale = std::ldexp(1.0, -level); // level px per level-0 px
    // screen corners in level-0 px: center +- half the window / zoom
    double half_w = view.window_size.x * 0.5 / view.zoom, half_h = view.window_size.y * 0.5 / view.zoom;
    double x0 = (view.center.x - half_w) * scale, x1 = (view.center.x + half_w) * scale;
    double y0 = (view.center.y - half_h) * scale, y1 = (view.center.y + half_h) * scale;

    auto clamp_to = [](double v, std::size_t size) {
        return static_cast<std::size_t>(std::clamp(v, 0.0, static_cast<double>(size)));
    };
    LevelRect r;
    r.x0 = clamp_to(std::floor(x0), l.width);
    r.y0 = clamp_to(std::floor(y0), l.height);
    r.x1 = clamp_to(std::ceil(x1), l.width);
    r.y1 = clamp_to(std::ceil(y1), l.height);
    return r;
}

FramePlan plan_frame(const ViewTransform& view, const PyramidGeometry& geom, std::size_t max_texture_size) {
    FramePlan plan;
    plan.level = choose_level(view.zoom, geom.level_count());
    const PyramidLevel& l = geom.levels[plan.level];
    LevelRect visible = visible_rect(view, geom, plan.level);

    // grow each axis by the margin, centered on the visible part, within the level and the texture limit
    auto grow = [&](std::size_t v0, std::size_t v1, std::size_t size, std::size_t& out0, std::size_t& out1) {
        std::size_t extent = v1 - v0;
        std::size_t margin = static_cast<std::size_t>(std::ceil(extent * kMarginFraction)) + 1;
        std::size_t want = std::min(extent + 2 * margin, max_texture_size);
        want = std::min(want, size);
        std::size_t center2 = v0 + v1; // twice the center
        std::size_t start = center2 / 2 >= want / 2 ? center2 / 2 - want / 2 : 0;
        start = std::min(start, size - want);
        out0 = start;
        out1 = start + want;
    };
    grow(visible.x0, visible.x1, l.width, plan.rect.x0, plan.rect.x1);
    grow(visible.y0, visible.y1, l.height, plan.rect.y0, plan.rect.y1);
    return plan;
}

bool covers(const FramePlan& frame, const ViewTransform& view, const PyramidGeometry& geom) {
    int level = choose_level(view.zoom, geom.level_count());
    return level == frame.level && frame.rect.contains(visible_rect(view, geom, level));
}

} // namespace enim
