#include "tile_selection.hpp"

#include <algorithm>
#include <cmath>

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

TileRect tiles_of(const LevelRect& rect) {
    if (rect.empty())
        return TileRect{};
    return TileRect{rect.x0 / kTileSize, rect.y0 / kTileSize, (rect.x1 + kTileSize - 1) / kTileSize,
                    (rect.y1 + kTileSize - 1) / kTileSize};
}

LevelRect tile_pixels(const PyramidGeometry& geom, int level, std::size_t tx, std::size_t ty) {
    const PyramidLevel& l = geom.levels[level];
    LevelRect r;
    r.x0 = tx * kTileSize;
    r.y0 = ty * kTileSize;
    r.x1 = std::min(r.x0 + kTileSize, l.width);
    r.y1 = std::min(r.y0 + kTileSize, l.height);
    return r;
}

SlotGrid ring_slots(Vec2f window_size, const PyramidGeometry& geom, int level, std::size_t max_texture_size) {
    const PyramidLevel& l = geom.levels[level];
    const std::size_t max_slots = std::max<std::size_t>(1, max_texture_size / kTileSize);
    auto slots = [&](float window, std::size_t tiles) {
        // visible level px: at most 2 per screen px, then misalignment and margins
        std::size_t want = static_cast<std::size_t>(std::ceil(2.0 * std::max(window, 1.0f) / kTileSize)) + 3;
        return std::max<std::size_t>(1, std::min({want, tiles, max_slots}));
    };
    return SlotGrid{slots(window_size.x, l.tiles_x), slots(window_size.y, l.tiles_y)};
}

TileRect tile_window(const ViewTransform& view, const PyramidGeometry& geom, int level, SlotGrid slots) {
    const PyramidLevel& l = geom.levels[level];
    TileRect visible = tiles_of(visible_rect(view, geom, level));
    if (visible.empty())
        return TileRect{};

    // grow by the margin, clamped to the level, then to the slots around the visible part's center
    auto fit = [](std::size_t v0, std::size_t v1, std::size_t tiles, std::size_t count, std::size_t& out0, std::size_t& out1) {
        std::size_t t0 = v0 > 0 ? v0 - 1 : 0;
        std::size_t t1 = std::min(v1 + 1, tiles);
        if (t1 - t0 > count) {
            std::size_t center = (v0 + v1) / 2;
            t0 = std::clamp(center >= count / 2 ? center - count / 2 : 0, t0, t1 - count);
            t1 = t0 + count;
        }
        out0 = t0;
        out1 = t1;
    };
    TileRect r;
    fit(visible.tx0, visible.tx1, l.tiles_x, slots.x, r.tx0, r.tx1);
    fit(visible.ty0, visible.ty1, l.tiles_y, slots.y, r.ty0, r.ty1);
    return r;
}

} // namespace enim
