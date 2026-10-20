#include <RGFW.h>
#include <cmath>
#include <plog/Log.h>
#include <window/window.hpp>

namespace enim {

#ifdef ENIM_OS_WIN
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
#endif

void Window::on_key_pressed(const RGFW_event& event) {
    switch (event.key.value) {
    case RGFW_keyEscape:
        RGFW_window_setShouldClose(event.common.win, 1);
        break;
    case RGFW_keyR:
        this->window_state.fit();
        this->request_redraw();
        break;
    default:
        break;
    }
}

void Window::on_resize(const RGFW_event& event) {
    PLOG_INFO << "Window resized to (" << event.update.w << ", " << event.update.h << ")";
    this->window_state.window_size.x = event.update.w;
    this->window_state.window_size.y = event.update.h;
    if (event.update.w <= 0 || event.update.h <= 0)
        return;
    this->request_redraw();
}

void Window::on_window_move(const RGFW_event& event) {}

void Window::on_mouse_wheel(const RGFW_event& event) {
    this->window_state.zoom_at(this->mouse_state.position, std::exp2(event.delta.y * 0.2f));
    this->request_redraw();
}

void Window::on_mouse_move(const RGFW_event& event) {
    this->mouse_state.position.x = event.mouse.x;
    this->mouse_state.position.y = event.mouse.y;

    if (this->mouse_state.left_button_down) {
        this->window_state.pan(this->mouse_state.position - this->mouse_state.last_LBD_position);
        this->mouse_state.last_LBD_position.x = this->mouse_state.position.x;
        this->mouse_state.last_LBD_position.y = this->mouse_state.position.y;
        this->request_redraw();
    }
}

void Window::on_mouse_down(const RGFW_event& event) {
    if (event.button.value == RGFW_mouseLeft) {
        this->mouse_state.left_button_down = true;
        this->mouse_state.last_LBD_position.x = this->mouse_state.position.x;
        this->mouse_state.last_LBD_position.y = this->mouse_state.position.y;
    } else if (event.button.value == RGFW_mouseRight) {
        this->mouse_state.right_button_down = true;
        this->mouse_state.last_RBD_position.x = this->mouse_state.position.x;
        this->mouse_state.last_RBD_position.y = this->mouse_state.position.y;
    }
}

void Window::on_mouse_up(const RGFW_event& event) {
    if (event.button.value == RGFW_mouseLeft) {
        this->mouse_state.left_button_down = false;
    } else if (event.button.value == RGFW_mouseRight) {
        this->mouse_state.right_button_down = false;
    }
}
} // namespace enim