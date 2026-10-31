#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <thread>

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

/// @brief Normal mode: the image pyramid lives on disk (one .pyr file in the cache), the ViewSampler turns the current
/// view into a frame on the CPU, and only that frame is uploaded. While a new frame is produced the previous one keeps
/// being drawn at its own image region, so it follows the camera.
struct TiledRenderer : public ImageRenderer {
    /// @param cache_dir Directory of the .pyr files.
    /// @param wakeup Wakes the main thread from any thread (frame bands queued, pyramid progress).
    TiledRenderer(std::unique_ptr<ImageSource> source, const LoadPlan& plan, std::filesystem::path image_path,
                  std::filesystem::path cache_dir, std::size_t max_texture_size, std::function<void()> wakeup);
    ~TiledRenderer() override;

    std::expected<void, enim::Error> init() override;
    void update(const ViewTransform& view) override;
    void draw(const ViewTransform& view) override;
    /// @brief True while update() has work: frame bands to upload, a finished build to join, or a frame to refine.
    /// In between the main thread waits: the sampler and the pyramid builder wake it.
    bool loading() const override;
    Vec2f image_size() const override;

  private:
    struct Frame {
        Texture texture;
        FramePlan plan;
        std::uint64_t generation = 0;
        int pass = 0;              // preview refinement pass
        std::uint64_t progress = 0; // pyramid progress when the frame was requested
        bool complete = false;     // every texel came from its own level: no need to refine
    };

    std::unique_ptr<ImageSource> source;
    LoadPlan plan;
    std::filesystem::path image_path;
    std::filesystem::path cache_dir;
    std::size_t max_texture_size;
    std::function<void()> wakeup;
    ImagePass image_pass;

    std::unique_ptr<PyramidStore> store;
    UploadQueue queue;
    std::optional<Frame> current; // the last complete frame: drawn, possibly stale
    std::optional<Frame> pending; // being sampled: drawn over current as its bands arrive
    std::uint64_t next_generation = 1;
    std::chrono::steady_clock::time_point last_refine;

    // destroyed first: the sampler and the build thread use the store, the queue and the source
    std::unique_ptr<ViewSampler> sampler;
    std::unique_ptr<PyramidBuilderSink> builder;
    std::chrono::steady_clock::time_point build_start;
    std::jthread build_thread;

    /// @brief Requests a frame. The pending frame is replaced and its texture reused when the size matches.
    void request_frame(const FramePlan& frame, int pass);
    void upload_bands();
    /// @brief Joins the build thread once the pyramid is done (or failed) and logs the result.
    void check_build();
    /// @brief The current frame should be resampled now: more preview samples, or rows written since it was requested.
    /// Rows written count at most every kRefineInterval (the builder wakes the main thread that often).
    bool refine_due() const;
};

} // namespace enim
