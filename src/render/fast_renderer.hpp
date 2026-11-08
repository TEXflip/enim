#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <thread>

#include <io/image_source.hpp>
#include <pipeline/pixel_converter.hpp>
#include <pipeline/texture_upload_sink.hpp>
#include <pipeline/upload_queue.hpp>
#include <plan/load_plan.hpp>
#include <render/gl/mipmap_builder.hpp>
#include <render/gl/raw_buffer.hpp>
#include <render/gl/texture.hpp>
#include <render/gpu_passes/convert_pass.hpp>
#include <render/image_pass.hpp>
#include <render/image_renderer.hpp>

namespace enim {

/// @brief Renders the whole image from a single display texture, for images that fit in VRAM.
struct FastRenderer : public ImageRenderer {
    std::unique_ptr<ImageSource> source;
    LoadPlan plan;
    ImagePass image_pass;
    Texture texture;
    bool flip_y = false;  // the texture rows are stored bottom-up
    bool mipmaps = true;  // trilinear minification; the chain is built progressively once the texture is complete

    /// @param wakeup Wakes the main thread from any thread (StreamUpload: when a decoded band is queued).
    FastRenderer(std::unique_ptr<ImageSource> source, const LoadPlan& plan, std::function<void()> wakeup)
        : source(std::move(source)), plan(plan), wakeup(std::move(wakeup)) {}

    std::expected<void, enim::Error> init() override;
    void update(const ViewTransform& view) override;
    void draw(const ViewTransform& view) override;
    /// @brief True while there is load work for the next update(). StreamUpload has work only when bands are queued:
    /// in between, the main thread waits for events and the decode thread wakes it.
    bool loading() const override {
        return this->convert_job || this->mip_job || (this->stream_job && this->stream_job->queue.pending());
    }
    bool toggle_mipmaps() override;
    Vec2f image_size() const override;

  private:
    std::function<void()> wakeup;

    /// @brief StreamUpload in progress: a decode thread converts rows and queues them, update() uploads them.
    /// Members are destroyed in reverse order: the thread is stopped and joined before the sink and the queue go.
    struct StreamJob {
        StreamJob(const PixelConverter& converter, std::size_t queue_bytes) : converter(converter), queue(queue_bytes) {}

        PixelConverter converter;
        BandQueue queue;
        std::unique_ptr<TextureUploadSink> sink;
        std::size_t rows_uploaded = 0;
        std::chrono::steady_clock::time_point start;
        std::jthread thread;
    };
    std::unique_ptr<StreamJob> stream_job;

    /// @brief GpuConvert in progress: one band of stored rows is uploaded and converted per update().
    struct GpuConvertJob {
        ConvertPass pass;
        RawBuffer band;
        std::size_t rows_per_band = 0;
        std::size_t next_row = 0; // storage row
        std::chrono::steady_clock::time_point start;
    };
    std::unique_ptr<GpuConvertJob> convert_job;

    /// @brief Framebuffers on the display texture's levels, set up while the texture is empty (see prepare_mip_builder).
    MipmapBuilder mip_builder;
    bool mip_blit = false; // the texture format can be blitted; otherwise glGenerateMipmap

    /// @brief Mip chain being built, a bounded number of texels per update(). Dropped (progress kept) when mipmaps are disabled.
    struct MipJob {
        int steps = 0;
        std::chrono::steady_clock::time_point start, last_step;
    };
    std::unique_ptr<MipJob> mip_job;

    std::expected<void, enim::Error> upload_direct();
    std::expected<void, enim::Error> start_stream_upload();
    void upload_stream_bands();
    /// @brief Creates the display texture (level 0 + mip storage, render target set up) before it gets any data.
    void create_display_texture();
    std::expected<void, enim::Error> start_gpu_convert();
    void convert_next_band();
    /// @brief Attaches the texture to the builder's framebuffers. Call it before the texture holds the image.
    void prepare_mip_builder();
    /// @brief Applies the mipmaps setting to the complete texture, starting or pausing the chain build.
    void apply_mipmaps();
    void build_next_mip_step();
};

} // namespace enim
