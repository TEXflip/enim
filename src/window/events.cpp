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
    // PLOG_DEBUG << "Window resized to " << event.update.w << "x" << event.update.h;
}

void Window::on_window_move(const RGFW_event& event) {
    
}

void Window::on_mouse_wheel(const RGFW_event& event) {
    float s_0 = this->window_state.scale; // old scale
    this->window_state.zoom -= event.delta.y * 0.1f; // Adjust zoom based on mouse wheel movement
    float s_1 = std::exp(this->window_state.zoom); // new scale
    this->window_state.scale = s_1;
    float sf = s_1 / s_0;

    const Shape& image_shape = this->image_view->reader().get_shape();
    float mouse_norm_x = s_0 * this->mouse_state.x / (float)this->window_state.size_x;
    float mouse_norm_y = s_0 * ((float)this->window_state.size_y - this->mouse_state.y) / (float)this->window_state.size_y;

    this->window_state.offset_x += mouse_norm_x * (1 - sf);
    this->window_state.offset_y += mouse_norm_y * (1 - sf);

    this->request_redraw();
    // PLOG_DEBUG << "scale: " << new_scale << " offset: (" << this->window_state.offset_x << ", " << this->window_state.offset_y << ")";
}

void Window::on_mouse_move(const RGFW_event& event) {
    this->mouse_state.x = event.mouse.x;
    this->mouse_state.y = event.mouse.y;

    // PLOG_DEBUG << "Mouse moved to: (" << this->mouse_state.x << ", " << this->mouse_state.y << ")";
    if (this->mouse_state.left_button_down) {
        this->window_state.offset_x -= (this->mouse_state.x - this->mouse_state.last_LBD_x) * this->window_state.scale / this->window_state.size_x;
        this->window_state.offset_y += (this->mouse_state.y - this->mouse_state.last_LBD_y) * this->window_state.scale / this->window_state.size_y;
        this->mouse_state.last_LBD_x = this->mouse_state.x;
        this->mouse_state.last_LBD_y = this->mouse_state.y;
        // PLOG_DEBUG << "Window offset after mouse move: (" << this->window_state.offset_x << ", " << this->window_state.offset_y << ")";
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