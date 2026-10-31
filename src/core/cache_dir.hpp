#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>

namespace enim {

struct Settings;

/// @brief Directory of the normal mode pyramids: --cache-dir, then ENIM_CACHE_DIR, then the platform's cache directory.
std::filesystem::path cache_root(const Settings& settings);

/// @brief Cache key of an image: MD5 of its file name and the bytes of its header (hex).
/// Hashing the whole file would take seconds for the images normal mode is for.
std::string cache_key(const std::filesystem::path& image_path, std::span<const std::uint8_t> header_bytes);

/// @brief <root>/<key>.pyr: the pyramid file of an image, levels 0..N.
std::filesystem::path pyramid_path(const std::filesystem::path& root, const std::string& key);

} // namespace enim
