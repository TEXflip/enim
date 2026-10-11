#include <RGFW.h>
#include <cmath>
#include <plog/Log.h>
#include <window/window.hpp>

namespace enim {
void Window::on_key_pressed(const RGFW_event& event) {
    if (event.key.value == RGFW_keyEscape) {
        RGFW_window_setShouldClose(event.common.win, 1);
    }
}

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

void Window::on_resize(const RGFW_event& event) {
    this->window_state.size_x = static_cast<float>(event.update.w);
    this->window_state.size_y = static_cast<float>(event.update.h);
    if (event.update.w <= 0 || event.update.h <= 0)
        return;
    this->request_redraw();
    PLOG_DEBUG << "Window resized to " << event.update.w << "x" << event.update.h;
}

void Window::on_window_move(const RGFW_event& event) {
    
}

void Window::on_mouse_wheel(const RGFW_event& event) {
    float old_scale = std::exp(this->window_state.zoom);
    this->window_state.zoom -= event.delta.y * 0.1f; // Adjust zoom based on mouse wheel movement
    float new_scale = std::exp(this->window_state.zoom);

    float mx = 2.0f * (this->mouse_state.x / (float)this->window_state.size_x) - 1.0f;
    float my = 1.0f - 2.0f * (this->mouse_state.y / (float)this->window_state.size_y);
    float factor = 1.0f / old_scale - 1.0f / new_scale;

    this->window_state.offset_x += mx * factor;
    this->window_state.offset_y += my * factor;

    this->request_redraw();
    // PLOG_DEBUG << "scale: " << new_scale << " offset: (" << this->window_state.offset_x << ", " << this->window_state.offset_y << ")";
}

void Window::on_mouse_move(const RGFW_event& event) {
    this->mouse_state.x = event.mouse.x;
    this->mouse_state.y = event.mouse.y;
    // PLOG_DEBUG << "Mouse moved to: (" << this->mouse_state.x << ", " << this->mouse_state.y << ")";
    if (this->mouse_state.left_button_down) {
        this->window_state.offset_x -= (this->mouse_state.x - this->mouse_state.last_LBD_x) * 0.01;
        this->window_state.offset_y += (this->mouse_state.y - this->mouse_state.last_LBD_y) * 0.01;
        this->mouse_state.last_LBD_x = this->mouse_state.x;
        this->mouse_state.last_LBD_y = this->mouse_state.y;
        this->request_redraw();
    }
}

void Window::on_mouse_down(const RGFW_event& event) {
    if (event.button.value == RGFW_mouseLeft) {
        this->mouse_state.left_button_down = true;
        this->mouse_state.last_LBD_x = this->mouse_state.x;
        this->mouse_state.last_LBD_y = this->mouse_state.y;
    } else if (event.button.value == RGFW_mouseRight) {
        this->mouse_state.right_button_down = true;
        this->mouse_state.last_RBD_x = this->mouse_state.x;
        this->mouse_state.last_RBD_y = this->mouse_state.y;
    }
    // PLOG_DEBUG << "Mouse State: (" << this->mouse_state.x << ", " << this->mouse_state.y << "), Left Button: " << this->mouse_state.left_button_down << ", Right Button: " << this->mouse_state.right_button_down;
}

void Window::on_mouse_up(const RGFW_event& event) {
    if (event.button.value == RGFW_mouseLeft) {
        this->mouse_state.left_button_down = false;
    } else if (event.button.value == RGFW_mouseRight) {
        this->mouse_state.right_button_down = false;
    }
    // PLOG_DEBUG << "Mouse State: (" << this->mouse_state.x << ", " << this->mouse_state.y << "), Left Button: " << this->mouse_state.left_button_down << ", Right Button: " << this->mouse_state.right_button_down;
}
} // namespace enim