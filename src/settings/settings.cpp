#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include <settings/settings.hpp>

#include "../errors.hpp"

namespace {

[[noreturn]] void exit_invalid_arguments() { std::exit(static_cast<int>(enim::ErrorCode::InvalidArguments)); }

void set_fast_mode_max_bytes(enim::Settings& settings, const char* value, const char* origin) {
    auto size = enim::parse_size(value);
    if (!size) {
        std::fprintf(stderr, "Invalid memory size '%s' in %s (expected e.g. 512M, 2G)\n", value, origin);
        exit_invalid_arguments();
    }
    settings.fast_mode_max_bytes = *size;
}

} // namespace

namespace enim {

std::optional<std::size_t> parse_size(std::string_view text) {
    constexpr std::size_t max_value = std::numeric_limits<std::size_t>::max();
    std::size_t value = 0;
    std::size_t digits = 0;
    while (digits < text.size() && text[digits] >= '0' && text[digits] <= '9') {
        auto digit = static_cast<std::size_t>(text[digits] - '0');
        if (value > (max_value - digit) / 10) // prevent overflow
            return std::nullopt;
        value = value * 10 + digit;
        ++digits;
    }
    if (digits == 0)
        return std::nullopt;

    std::string_view unit = text.substr(digits);
    if (unit.ends_with("iB"))
        unit.remove_suffix(2);
    else if (unit.ends_with("B") || unit.ends_with("b"))
        unit.remove_suffix(1);

    if (unit.size() > 1)
        return std::nullopt;

    char prefix = unit.empty() ? '\0' : static_cast<char>(std::toupper(static_cast<unsigned char>(unit[0])));
    unsigned shift = 0;
    switch (prefix) {
    case '\0': shift = 0; break;
    case 'K': shift = 10; break;
    case 'M': shift = 20; break;
    case 'G': shift = 30; break;
    case 'T': shift = 40; break;
    default: return std::nullopt;
    }

    if (value > (std::numeric_limits<std::size_t>::max() >> shift)) // prevent overflow when shifting
        return std::nullopt;
    return value << shift;
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
    std::printf("  --profile              Log profiling milestones (time since startup, thread) to stderr\n");
    std::printf("  --exit-after-load     Tests: quit once the image is loaded; exit code 1 if an error was logged\n");
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
        } else if (std::strcmp(argv[i], "--profile") == 0) {
            settings.profile = true;
        } else if (std::strcmp(argv[i], "--exit-after-load") == 0) {
            settings.exit_after_load = true;
        } else if (std::strcmp(argv[i], "--fast-max-mem") == 0) {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "--fast-max-mem needs a value\n");
                exit_invalid_arguments();
            }
            set_fast_mode_max_bytes(settings, argv[++i], "--fast-max-mem");
        } else if (std::strcmp(argv[i], "--cache-dir") == 0) {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "--cache-dir needs a value\n");
                exit_invalid_arguments();
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
