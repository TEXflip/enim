#include "cache_dir.hpp"

#include <cstdlib>

#include <core/md5.hpp>
#include <settings/settings.hpp>

namespace {

std::filesystem::path env_path(const char* name) {
    const char* value = std::getenv(name);
    return value && *value ? std::filesystem::path(value) : std::filesystem::path();
}

} // namespace

namespace enim {

std::filesystem::path cache_root(const Settings& settings) {
    if (!settings.cache_dir.empty())
        return settings.cache_dir;
    if (auto dir = env_path("ENIM_CACHE_DIR"); !dir.empty())
        return dir;
#if defined(ENIM_OS_WIN)
    if (auto dir = env_path("LOCALAPPDATA"); !dir.empty())
        return dir / "enim" / "cache";
#elif defined(ENIM_OS_MAC)
    if (auto dir = env_path("HOME"); !dir.empty())
        return dir / "Library" / "Caches" / "enim";
#else
    if (auto dir = env_path("XDG_CACHE_HOME"); !dir.empty())
        return dir / "enim";
    if (auto dir = env_path("HOME"); !dir.empty())
        return dir / ".cache" / "enim";
#endif
    return std::filesystem::temp_directory_path() / "enim";
}

std::string cache_key(const std::filesystem::path& image_path, std::span<const std::uint8_t> header_bytes) {
    Md5 md5;
    std::u8string name = image_path.filename().u8string();
    md5.update(std::string_view(reinterpret_cast<const char*>(name.data()), name.size()));
    md5.update(header_bytes);
    return Md5::hex(md5.finish());
}

} // namespace enim
