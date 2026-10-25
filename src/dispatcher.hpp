#pragma once

#include <cstdio>
#include <cstring>
#include <expected>
#include <filesystem>
#include <fstream>
#include <functional>
#include <unordered_map>

#include <plog/Log.h>

#include "errors.hpp"
#include "formats.hpp"
#include <render/image_renderer.hpp>
#include <settings/settings.hpp>

#include "bmp.hpp"

using FileHeaderFactory = std::function<std::unique_ptr<enim::FileReader>()>;
namespace enim {

const std::expected<enim::FileFormat, enim::Error> detect_format(const std::filesystem::path& path);

/// @brief Dispatches the appropriate image renderer
/// @param settings The settings containing image path and other configurations.
/// @param max_texture_size The maximum allowed texture size in OpenGL for this system.
/// @return A std::expected containing a unique pointer to the (not yet initialized) ImageRenderer on success, or an Error on failure.
std::expected<std::unique_ptr<enim::ImageRenderer>, enim::Error> dispatch(const Settings& settings, int max_texture_size);

} // namespace enim