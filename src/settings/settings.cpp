#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <settings/settings.hpp>

namespace {

void set_fast_mode_max_bytes(enim::Settings& settings, const char* value, const char* origin) {
    auto size = enim::parse_size(value);
    if (!size) {
        std::fprintf(stderr, "Invalid memory size '%s' in %s (expected e.g. 512M, 2G)\n", value, origin);
        std::exit(1);
    }
    settings.fast_mode_max_bytes = *size;
}

} // namespace

namespace enim {

std::optional<std::size_t> parse_size(std::string_view text) {
    double value = 0;
    auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || value < 0)
        return std::nullopt;

    std::string_view unit(end, text.data() + text.size() - end);
    if (unit.ends_with("iB"))
        unit.remove_suffix(2);
    else if (unit.ends_with("B") || unit.ends_with("b"))
        unit.remove_suffix(1);

    double multiplier = 1;
    if (unit.empty())
        multiplier = 1;
    else if (unit == "K" || unit == "k")
        multiplier = 1024.0;
    else if (unit == "M" || unit == "m")
        multiplier = 1024.0 * 1024.0;
    else if (unit == "G" || unit == "g")
        multiplier = 1024.0 * 1024.0 * 1024.0;
    else if (unit == "T" || unit == "t")
        multiplier = 1024.0 * 1024.0 * 1024.0 * 1024.0;
    else
        return std::nullopt;

    return static_cast<std::size_t>(value * multiplier);
}

void print_help(const char* program_name) {
    std::printf("Usage: %s [options] <image>\n", program_name);
    std::printf("Options:\n");
    std::printf("  --help                 Show this help message\n");
    std::printf("  -d, --debug            Enable debug logging\n");
    std::printf("  --force-fast-mode      Enable fast mode\n");
    std::printf("  --force-normal-mode    Enable normal mode\n");
    std::printf("  --fast-max-mem <size>  GPU memory budget for fast mode, e.g. 512M, 2G (default 1G,\n");
    std::printf("                         or the ENIM_FAST_MAX_MEM environment variable)\n");
    std::printf("  --cache-dir <dir>      Where normal mode keeps the image pyramids (default: the user's cache directory,\n");
    std::printf("                         or the ENIM_CACHE_DIR environment variable)\n");
    std::printf("  --force-gpu-convert    Debug: convert uncompressed images on the GPU even if they can be uploaded as is\n");
    std::printf("  --force-stream-upload  Debug: decode and convert uncompressed images on the CPU\n");
}


Settings parse_args(int argc, char* argv[]) {

    Settings settings;

    if (const char* env = std::getenv("ENIM_FAST_MAX_MEM"); env && *env)
        set_fast_mode_max_bytes(settings, env, "ENIM_FAST_MAX_MEM");

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--debug") == 0 || std::strcmp(argv[i], "-d") == 0) {
            settings.debug = true;
        } else if (std::strcmp(argv[i], "--help") == 0) {
            print_help(argv[0]);
            std::exit(0);
        } else if (std::strcmp(argv[i], "--force-fast-mode") == 0) {
            settings.force_fast_mode = true;
        } else if (std::strcmp(argv[i], "--force-normal-mode") == 0) {
            settings.force_normal_mode = true;
        } else if (std::strcmp(argv[i], "--force-gpu-convert") == 0) {
            settings.force_gpu_convert = true;
        } else if (std::strcmp(argv[i], "--force-stream-upload") == 0) {
            settings.force_stream_upload = true;
        } else if (std::strcmp(argv[i], "--fast-max-mem") == 0) {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "--fast-max-mem needs a value\n");
                std::exit(1);
            }
            set_fast_mode_max_bytes(settings, argv[++i], "--fast-max-mem");
        } else if (std::strcmp(argv[i], "--cache-dir") == 0) {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "--cache-dir needs a value\n");
                std::exit(1);
            }
            settings.cache_dir = argv[++i];
        } else {
            settings.image_path = argv[i];
        }
    }

#ifdef DEBUG
    settings.debug = true;
#endif

    return settings;
}

} // namespace enim
