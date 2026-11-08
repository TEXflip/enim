#pragma once

#include <algorithm>
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
};

/// @brief Rectangle of tiles of one pyramid level, [tx0, tx1) x [ty0, ty1).
struct TileRect {
    std::size_t tx0 = 0, ty0 = 0, tx1 = 0, ty1 = 0;

    std::size_t width() const { return this->tx1 - this->tx0; }
    std::size_t height() const { return this->ty1 - this->ty0; }
    bool empty() const { return this->tx1 <= this->tx0 || this->ty1 <= this->ty0; }
    /// @brief The tiles in both rectangles (empty if none).
    TileRect intersect(const TileRect& o) const {
        return TileRect{std::max(this->tx0, o.tx0), std::max(this->ty0, o.ty0), std::min(this->tx1, o.tx1),
                        std::min(this->ty1, o.ty1)};
    }
    /// @brief Calls fn(tx, ty) for each tile, row by row.
    template <typename Fn>
    void for_each(Fn&& fn) const {
        for (std::size_t ty = this->ty0; ty < this->ty1; ++ty)
            for (std::size_t tx = this->tx0; tx < this->tx1; ++tx)
                fn(tx, ty);
    }
    /// @brief True if pred(tx, ty) holds for every tile (true for an empty rectangle).
    template <typename Pred>
    bool all_of(Pred&& pred) const {
        for (std::size_t ty = this->ty0; ty < this->ty1; ++ty)
            for (std::size_t tx = this->tx0; tx < this->tx1; ++tx)
                if (!pred(tx, ty))
                    return false;
        return true;
    }
    template <typename Pred>
    bool any_of(Pred&& pred) const {
        return !this->all_of([&](std::size_t tx, std::size_t ty) { return !pred(tx, ty); });
    }
};

/// @brief Size of a tile ring, in tiles.
struct SlotGrid {
    std::size_t x = 0, y = 0;

    std::size_t count() const { return this->x * this->y; }
    bool operator==(const SlotGrid&) const = default;
};

/// @brief Pyramid level for a zoom: one screen px covers 1 to 2 px of it (less than 1 when zoomed in past 100%).
int choose_level(float zoom, int level_count);

/// @brief The pixels of a level visible in the view, clamped to the level.
LevelRect visible_rect(const ViewTransform& view, const PyramidGeometry& geom, int level);

/// @brief The tiles of a level that the pixels of rect touch.
TileRect tiles_of(const LevelRect& rect);

/// @brief The pixels of tile (tx, ty) of a level, clamped to the level: edge tiles are smaller than kTileSize.
LevelRect tile_pixels(const PyramidGeometry& geom, int level, std::size_t tx, std::size_t ty);

/// @brief Tile slots of the ring texture for a level: enough for the visible tiles at any zoom that selects this level
/// (one screen px covers up to 2 level px), plus one tile of misalignment and a one-tile margin on each side. Depends
/// only on the window size, so zooming within a level keeps the ring. At most the level's tiles and max_texture_size.
SlotGrid ring_slots(Vec2f window_size, const PyramidGeometry& geom, int level, std::size_t max_texture_size);

/// @brief The tiles a ring holds for the view: the visible ones plus a one-tile margin, clamped to the level and to
/// the slots (centered on the visible part), so that every tile maps to its own slot (tx mod slots.x, ty mod slots.y).
TileRect tile_window(const ViewTransform& view, const PyramidGeometry& geom, int level, SlotGrid slots);

} // namespace enim
