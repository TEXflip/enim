#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace enim {

constexpr std::size_t kDefaultFastModeMaxBytes = std::size_t{1} << 30; // 1 GiB

/// @brief Structure to hold application settings parsed from command-line arguments.
struct Settings {
    bool debug = false;
    bool force_fast_mode = false;
    bool force_normal_mode = false;
    bool force_gpu_convert = false; // debug: convert on the GPU even when the layout could be uploaded directly
    bool force_stream_upload = false; // debug: decode and convert uncompressed images on the CPU (StreamUpload)
    std::size_t fast_mode_max_bytes = kDefaultFastModeMaxBytes; // --fast-max-mem, then ENIM_FAST_MAX_MEM
    std::string cache_dir; // --cache-dir; empty: ENIM_CACHE_DIR, then the platform's cache directory (core/cache_dir)
    std::string image_path;
};

/// @brief Parses a memory size such as "512M", "2G", "1.5GiB" or "1048576" (binary units).
/// @return The size in bytes, or nullopt if the string is not a valid size.
std::optional<std::size_t> parse_size(std::string_view text);

/// @brief Print the help message for the application.
/// @param program_name The name of the program (typically argv[0]).
void print_help(const char* program_name);

/// @brief Parse command-line arguments and populate the Settings structure.
/// @param argc The number of command-line arguments.
/// @param argv The array of command-line argument strings.
/// @return A Settings structure populated based on the provided arguments.
Settings parse_args(int argc, char* argv[]);

} // namespace enim
