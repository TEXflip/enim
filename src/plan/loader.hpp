#pragma once

#include <expected>
#include <memory>

#include <errors.hpp>
#include <plan/load_plan.hpp>
#include <render/image_renderer.hpp>
#include <settings/settings.hpp>

namespace enim {

/// @brief Reads the GPU limits the planner needs. The GL context must be current.
GpuLimits query_gpu_limits();

/// @brief Opens the image, plans how to load it and creates the matching renderer.
/// @return The renderer, not yet initialized, or an Error on failure.
std::expected<std::unique_ptr<ImageRenderer>, enim::Error> load(const Settings& settings, const GpuLimits& gpu);

} // namespace enim
