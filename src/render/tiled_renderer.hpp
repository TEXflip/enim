#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

#include <io/image_source.hpp>
#include <pipeline/upload_queue.hpp>
#include <plan/load_plan.hpp>
#include <pyramid/pyramid_builder.hpp>
#include <pyramid/pyramid_store.hpp>
#include <pyramid/tile_selection.hpp>
#include <pyramid/view_sampler.hpp>
#include <render/gl/texture.hpp>
#include <render/image_pass.hpp>
#include <render/image_renderer.hpp>

namespace enim {

/// @brief Normal mode: the image pyramid lives on disk (one .pyr file in the cache) and the GPU holds only the tiles of
/// the current level around the view, in a ring texture: tile (tx, ty) lives in slot (tx mod slots.x, ty mod slots.y).
/// Each update() finds the tiles the view needs that the ring doesn't hold yet (or holds a preview of), the
/// ViewSampler produces them on the CPU and only those are uploaded. After a level change the previous ring keeps
/// being drawn under the new one until the visible tiles arrive.
struct TiledRenderer : public ImageRenderer {
    /// @param cache_dir Directory of the .pyr files.
    /// @param wakeup Wakes the main thread from any thread (tiles queued, pyramid progress).
    TiledRenderer(std::unique_ptr<ImageSource> source, const LoadPlan& plan, std::filesystem::path image_path,
                  std::filesystem::path cache_dir, std::size_t max_texture_size, std::function<void()> wakeup);
    ~TiledRenderer() override;

    std::expected<void, enim::Error> init() override;
    void update(const ViewTransform& view) override;
    void draw(const ViewTransform& view) override;
    /// @brief True while update() has work: tiles to upload, a finished build to join, or a tile to refine.
    /// In between the main thread waits: the sampler and the pyramid builder wake it.
    bool loading() const override;
    Vec2f image_size() const override;

  private:
    static constexpr std::size_t kNoTile = std::numeric_limits<std::size_t>::max();

    struct Slot {
        std::size_t tx = kNoTile, ty = kNoTile; // the tile the slot holds or waits for
        int requested_pass = -1;                // last pass asked to the sampler, -1 = never
        int uploaded_pass = -1;                 // pass of the texels in the slot, -1 = transparent
        bool complete = false;                  // the texels came from the tile's own level: no refining
        std::uint64_t progress = 0;             // pyramid progress when last requested
    };

    struct Ring {
        Texture texture;
        int level = 0;
        SlotGrid slots;
        TileRect window; // the tiles the slots are assigned to
        std::vector<Slot> slot_state;

        std::size_t slot_x(std::size_t tx) const { return tx % this->slots.x; }
        std::size_t slot_y(std::size_t ty) const { return ty % this->slots.y; }
        std::size_t slot_index(std::size_t tx, std::size_t ty) const { return this->slot_y(ty) * this->slots.x + this->slot_x(tx); }
        Slot& slot(std::size_t tx, std::size_t ty) { return this->slot_state[this->slot_index(tx, ty)]; }
        const Slot& slot(std::size_t tx, std::size_t ty) const { return this->slot_state[this->slot_index(tx, ty)]; }
    };

    std::unique_ptr<ImageSource> source;
    LoadPlan plan;
    std::filesystem::path image_path;
    std::filesystem::path cache_dir;
    std::size_t max_texture_size;
    std::function<void()> wakeup;
    ImagePass image_pass;

    std::unique_ptr<PyramidStore> store;
    TileQueue queue;
    UploadParams upload_params;
    std::optional<Ring> current;  // the ring of the view's level: tiles are requested and uploaded into it
    std::optional<Ring> previous; // the ring of the level shown before: drawn under current until its tiles arrive
    std::vector<std::uint8_t> zero_tile; // transparent tile, to clear reassigned slots
    std::uint64_t next_generation = 1;
    std::chrono::steady_clock::time_point last_refine;

    // destroyed first: the sampler and the build thread use the store, the queue and the source
    std::unique_ptr<ViewSampler> sampler;
    std::unique_ptr<PyramidBuilderSink> builder;
    std::chrono::steady_clock::time_point build_start;
    std::jthread build_thread;

    /// @brief Makes a new current ring (all slots transparent) and keeps the old one as previous if it shows anything.
    void start_ring(int level, SlotGrid slots);
    /// @brief Assigns the slots to the tiles of the new window, clearing the slots that held another tile.
    void assign_window(Ring& ring, const TileRect& window);
    /// @brief Posts the tiles of the window that are missing or due for refinement (with those still being sampled).
    void request_tiles(Ring& ring, const ViewTransform& view);
    void upload_tiles();
    /// @brief Uploads width x height tightly packed texels to the slot of tile (tx, ty).
    void upload_to_slot(const Ring& ring, std::size_t tx, std::size_t ty, std::size_t width, std::size_t height,
                        const std::uint8_t* pixels) const;
    /// @brief Joins the build thread once the pyramid is done (or failed) and logs the result.
    void check_build();
    /// @brief The slot's tile should be resampled now: more preview samples, or rows written since it was requested.
    /// Rows written count at most every kRefineInterval (the builder wakes the main thread that often).
    bool refine_due(const Slot& slot, std::chrono::steady_clock::time_point now) const;
    /// @brief Every tile of the ring that the view shows has texels (of any pass).
    bool shows_view(const Ring& ring, const ViewTransform& view) const;
    void draw_ring(const Ring& ring, const ViewTransform& view) const;
};

} // namespace enim
