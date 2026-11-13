#include "loader.hpp"

#include <glad/glad.h>
#include <plog/Log.h>

#include <core/cache_dir.hpp>
#include <core/wakeup.hpp>
#include <io/detect.hpp>

#include "../logging.hpp"
#include <render/fast_renderer.hpp>
#include <render/tiled_renderer.hpp>

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
    PLOG_PROFILE << "source opened";

    LoadPlan plan = make_plan(source->info(), source->caps(), gpu, settings);
    PLOG_PROFILE << "plan made";

    PLOGI << "Mode: " << to_string(plan.mode) << ", fast mode needs " << (plan.fast_bytes >> 20) << " MiB of "
              << (settings.fast_mode_max_bytes >> 20) << " MiB budget";

    if (plan.mode == Mode::Normal) {
        PLOGI << "Normal mode level 0: " << (plan.level0 == Level0Source::SourceMmap ? "source mmap" : "decoded to disk");
        return std::make_unique<TiledRenderer>(std::move(source), plan, settings.image_path, cache_root(settings),
                                               gpu.max_texture_size, wake_main_thread);
    }

    PLOGI << "Fast mode strategy: " << to_string(plan.fast);
    return std::make_unique<FastRenderer>(std::move(source), plan, wake_main_thread);
}

} // namespace enim
