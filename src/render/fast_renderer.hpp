#pragma once

#include <chrono>
#include <memory>

#include <io/image_source.hpp>
#include <plan/load_plan.hpp>
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
    bool flip_y = false; // the texture rows are stored bottom-up

    FastRenderer(std::unique_ptr<ImageSource> source, const LoadPlan& plan) : source(std::move(source)), plan(plan) {}

    std::expected<void, enim::Error> init() override;
    void update(const ViewTransform& view) override;
    void draw(const ViewTransform& view) override;
    bool loading() const override { return this->convert_job != nullptr; }
    Vec2f image_size() const override;

  private:
    /// @brief GpuConvert in progress: one band of stored rows is uploaded and converted per update().
    struct GpuConvertJob {
        ConvertPass pass;
        RawBuffer band;
        std::size_t rows_per_band = 0;
        std::size_t next_row = 0; // storage row
        std::chrono::steady_clock::time_point start;
    };
    std::unique_ptr<GpuConvertJob> convert_job;

    std::expected<void, enim::Error> upload_direct();
    std::expected<void, enim::Error> start_gpu_convert();
    void convert_next_band();
};

} // namespace enim
