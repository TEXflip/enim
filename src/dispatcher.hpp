#pragma once

#include <cstdio>
#include <cstring>
#include <expected>
#include <filesystem>
#include <fstream>
#include <functional>
#include <print>
#include <unordered_map>

#include <plog/Log.h>

#include "errors.hpp"
#include "formats.hpp"
#include "image_view.hpp"
#include <settings/settings.hpp>

#include "bmp.hpp"

using FileHeaderFactory = std::function<std::unique_ptr<enim::FileReader>()>;
namespace enim {

const std::expected<enim::FileFormat, enim::Error> detect_format(const std::filesystem::path& path);
std::expected<std::unique_ptr<enim::ImageView>, enim::Error> dispatch(const Settings& settings);

} // namespace enim