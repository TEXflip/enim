#include <RGFW.h>
#include <plog/Log.h>
#include <window/window.hpp>

namespace enim {
void Window::on_key_pressed(const RGFW_event& event) {
    if (event.key.value == RGFW_keyEscape) {
        RGFW_window_setShouldClose(event.common.win, 1);
    }
}

LRESULT CALLBACK SubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR ref) {
    auto* self = reinterpret_cast<enim::Window*>(ref);
    switch (msg) {
    case WM_SIZE: {
        RGFW_event resize_event{};
        resize_event.common.win = self->win;
        resize_event.update.w = LOWORD(lp);
        resize_event.update.h = HIWORD(lp);
        self->on_resize(resize_event);
        break;
    }

    default:
        break;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

void Window::on_resize(const RGFW_event& event) {
    if (event.update.w <= 0 || event.update.h <= 0)
        return;
    if (!this->redraw.exchange(true)) {
        this->redraw.notify_one();
        PLOG_INFO << "Window resized to " << event.update.w << "x" << event.update.h;
    }
}

void Window::on_mouse_move(const RGFW_event& event) {
    // Handle mouse movement event
}

void Window::on_mouse_wheel(const RGFW_event& event) {
    // event.delta.x;
    // event.delta.y;
    PLOG_INFO << "Mouse wheel moved: (" << event.delta.x << ", " << event.delta.y << ")";
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
}; // namespace enim