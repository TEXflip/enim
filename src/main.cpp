#ifdef DEBUG
    #define RGFW_DEBUG
#endif

#define GL_SILENCE_DEPRECATION
#define RGFW_OPENGL
#define RGFW_IMPLEMENTATION
#define RGL_LOAD_IMPLEMENTATION
#include "RGFW.h"

#ifdef RGFW_MACOS
    #include <OpenGL/gl.h>
#else
    #include <GL/gl.h>
#endif

#include "dispatcher.hpp"
#include "image_view.hpp"
#include "settings.hpp"
#include "logging.hpp"

#include "window/setup.hpp"
#include "window/loop.hpp"

#include <filesystem>

int main(int argc, char* argv[]) {
    Settings settings = parse_args(argc, argv);

    setup_logger(settings);
    
    if (!settings.image_path.empty()) {
        auto view_result = dispatch(settings);
        if (!view_result) {
            const auto& error = view_result.error();
            PLOG_ERROR << error.message;
            std::exit(static_cast<int>(error.code));
        }
    }

    auto result = setup_window();
    if (!result) {
        const auto& error = result.error();
        PLOG_ERROR << error.message;
        std::exit(static_cast<int>(error.code));
    }
    WindowData window_data = result.value();

    main_loop(window_data);

    glDeleteVertexArrays(1, &window_data.vao);
    glDeleteBuffers(1, &window_data.vbo);
    glDeleteBuffers(1, &window_data.ebo);
    glDeleteProgram(window_data.main_program);

    RGFW_window_close(window_data.win);
    RGFW_deinit();

    return 0;
}