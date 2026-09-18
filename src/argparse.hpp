#include <cstring>
#include <vector>
#include <string>

struct ArgumentSettings {
    bool debug = false;
    bool help = false;
    std::string image_path;
};

ArgumentSettings parse_args(int argc, char* argv[]) {
    ArgumentSettings settings;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--debug") == 0 || std::strcmp(argv[i], "-d") == 0) {
            settings.debug = true;
        } else if (std::strcmp(argv[i], "--help") == 0) {
            settings.help = true;
        } else {
            settings.image_path = argv[i];
        }
    }

    #ifdef DEBUG
        settings.debug = true;
    #endif

    return settings;
}
