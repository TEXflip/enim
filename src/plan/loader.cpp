#include "loader.hpp"

#include <glad/glad.h>
#include <plog/Log.h>

#include <io/detect.hpp>
#include <render/fast_renderer.hpp>

namespace enim {

GpuLimits query_gpu_limits() {
    GLint max_texture_size = 0, max_texture_buffer_size = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size);
    glGetIntegerv(GL_MAX_TEXTURE_BUFFER_SIZE, &max_texture_buffer_size);
    return GpuLimits{static_cast<std::size_t>(max_texture_size), static_cast<std::size_t>(max_texture_buffer_size)};
}

std::expected<std::unique_ptr<ImageRenderer>, enim::Error> load(const Settings& settings, const GpuLimits& gpu) {
    auto source_result = open_source(settings.image_path);
    if (!source_result)
        return std::unexpected(source_result.error());
    std::unique_ptr<ImageSource> source = std::move(source_result.value());

    LoadPlan plan = make_plan(source->info(), source->caps(), gpu, settings);

    PLOG_INFO << "Mode: " << to_string(plan.mode) << ", fast mode needs " << (plan.fast_bytes >> 20) << " MiB of "
              << (settings.fast_mode_max_bytes >> 20) << " MiB budget";

    if (plan.mode == Mode::Normal) {
        // TODO: return the tiled renderer once normal mode is implemented
        const ImageInfo& info = source->info();
        if (info.width > gpu.max_texture_size || info.height > gpu.max_texture_size)
            return std::unexpected(enim::Error{enim::ErrorCode::ImageLoadError,
                                               "Image exceeds GL_MAX_TEXTURE_SIZE and normal mode is not implemented yet"});
        PLOG_WARNING << "Normal mode not implemented yet, falling back to fast mode";
        Settings fast_settings = settings;
        fast_settings.force_fast_mode = true;
        fast_settings.force_normal_mode = false;
        plan = make_plan(info, source->caps(), gpu, fast_settings);
    }

    PLOG_INFO << "Fast mode strategy: " << to_string(plan.fast);
    return std::make_unique<FastRenderer>(std::move(source), plan);
}

} // namespace enim
