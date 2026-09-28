#include <window/window.hpp>
#include <RGFW.h>

namespace enim {
    void Window::on_key_pressed(const RGFW_event& event) {
        if (event.key.value == RGFW_keyEscape) {
            RGFW_window_setShouldClose(event.common.win, 1);
        }
    }

    void Window::on_resize(const RGFW_event& event) {
        glViewport(0, 0, event.update.w, event.update.h);
    }
};