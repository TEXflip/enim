#include "dispatcher.hpp"
#include "image_view.hpp"
#include "settings.hpp"
#include "logging.hpp"

#define RGFW_IMPLEMENTATION
#include "RGFW.h"

#include <filesystem>

void keyfunc(const RGFW_event* event) {
    if (event->key.value == RGFW_keyEscape) {
        RGFW_window_setShouldClose(event->common.win, 1);
    }
}

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

    RGFW_init("example", 0); /* load OpenGL, Vulkan or EGL functions here with RGFW_initOpenGL, RGFW_initEGL or RGFW_initVulkan */
    
    RGFW_window* win = RGFW_createWindow("a window", 0, 0, 800, 600, RGFW_windowCenter | RGFW_windowNoResize);

    RGFW_setEventCallback(RGFW_keyPressed, keyfunc); // you can use callbacks like this if you want

    i32 mouseX, mouseY;

    while (RGFW_window_shouldClose(win) == RGFW_FALSE) {
        RGFW_event event;
        while (RGFW_window_checkEvent(win, &event)) {  // or RGFW_pollEvents(); if you only want callbacks / state checking
            RGFW_window_getMouse(win, &mouseX, &mouseY);

            if (event.type == RGFW_mouseButtonPressed && event.button.value == RGFW_mouseLeft) {
                printf("You clicked at x: %d, y: %d\n", mouseX, mouseY);
            }
        }

        /* state checking */
        if (RGFW_isMousePressed(RGFW_mouseRight)) {
            printf("The right mouse button was clicked at x: %d, y: %d\n", mouseX, mouseY);
        }
    }

    RGFW_window_close(win);
    RGFW_deinit();

    return 0;
}