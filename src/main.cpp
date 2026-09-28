#ifdef DEBUG
    #define RGFW_DEBUG
#endif

#define GL_SILENCE_DEPRECATION
#define RGFW_OPENGL
#define RGFW_IMPLEMENTATION
#define RGL_LOAD_IMPLEMENTATION
#include "RGFW.h"

// #ifdef RGFW_MACOS
//     #include <OpenGL/gl.h>
// #else
//     #include <GL/gl.h>
// #endif

#include "dispatcher.hpp"
#include "image_view.hpp"
#include "settings.hpp"
#include "logging.hpp"

#include "window/window.hpp"

#include <filesystem>

int main(int argc, char* argv[]) {
    enim::Settings settings = enim::parse_args(argc, argv);

    enim::setup_logger(settings);

    enim::Window window(settings);

    auto result = window.setup();
    if (!result) {
        const auto& error = result.error();
        PLOG_ERROR << error.message;
        std::exit(static_cast<int>(error.code));
    }
    window.load_image();

    window.run();

    return 0;
}