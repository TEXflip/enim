#include <plog/Log.h>
#include <window/window.hpp>
#include <RGFW.h>

namespace enim {
    void Window::on_key_pressed(const RGFW_event& event) {
        if (event.key.value == RGFW_keyEscape) {
            RGFW_window_setShouldClose(event.common.win, 1);
        }
    }

    void Window::on_resize(const RGFW_event& event) {
        PLOG_INFO << "Window resized to " << event.update.w << "x" << event.update.h;
        if (event.update.w <= 0 || event.update.h <= 0) return; // minimized window
        this->redraw.store(true);
    }

    void Window::on_mouse_move(const RGFW_event& event) {
        // Handle mouse movement event
    }

    void Window::on_window_move(const RGFW_event& event) {
        // Handle mouse movement event
    }

    void Window::on_mouse_down(const RGFW_event& event) {
        // Handle mouse button down event
    }

    void Window::on_mouse_up(const RGFW_event& event) {
        // Handle mouse button up event
    }
};