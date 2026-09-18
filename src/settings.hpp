#pragma once

#include <cstring>
#include <string>
#include <vector>

/// @brief Structure to hold application settings parsed from command-line arguments.
struct Settings {
    bool debug = false;
    bool force_fast_mode = false;
    bool force_normal_mode = false;
    std::string image_path;
};

/// @brief Print the help message for the application.
/// @param program_name The name of the program (typically argv[0]).
void print_help(const char* program_name) {
    std::printf("Usage: %s [options]\n", program_name);
    std::printf("Options:\n");
    std::printf("  --help        Show this help message\n");
    std::printf("  -d, --debug   Enable debug logging\n");
    std::printf("  --force-fast-mode   Enable fast mode\n");
    std::printf("  --force-normal-mode Enable normal mode\n");
}

/// @brief Parse command-line arguments and populate the Settings structure.
/// @param argc The number of command-line arguments.
/// @param argv The array of command-line argument strings.
/// @return A Settings structure populated based on the provided arguments.
Settings parse_args(int argc, char* argv[]) {

    Settings settings;
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
        } else {
            settings.image_path = argv[i];
        }
    }

#ifdef DEBUG
    settings.debug = true;
#endif

    return settings;
}
