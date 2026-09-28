#pragma once

#include <string>

namespace enim {

/// @brief Structure to hold application settings parsed from command-line arguments.
struct Settings {
    bool debug = false;
    bool force_fast_mode = false;
    bool force_normal_mode = false;
    std::string image_path;
};

/// @brief Print the help message for the application.
/// @param program_name The name of the program (typically argv[0]).
void print_help(const char* program_name);

/// @brief Parse command-line arguments and populate the Settings structure.
/// @param argc The number of command-line arguments.
/// @param argv The array of command-line argument strings.
/// @return A Settings structure populated based on the provided arguments.
Settings parse_args(int argc, char* argv[]);

} // namespace enim