#pragma once

#include <expected>
#include <glad/glad.h>
#include <cstdint>
#include <memory>
#include <settings/settings.hpp>
#include <errors.hpp>
#include <render/image_renderer.hpp>
#include <semaphore>
#include <limits>
#include <linalgebra.hpp>
#include <view/view_transform.hpp>


#ifdef ENIM_OS_WIN
    #include <windows.h>
    #include <windowsx.h>
    #include <commctrl.h>
#endif

struct RGFW_window;
union RGFW_event;

namespace enim {

struct MouseState {
    Vec2f position{0.0f, 0.0f};
    bool left_button_down = false;
    bool right_button_down = false;

    Vec2f last_LBD_position{std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN()};
    Vec2f last_RBD_position{std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN()};
};

struct Window {
    RGFW_window* win;

    std::unique_ptr<enim::ImageRenderer> renderer = nullptr;
    Settings& settings;
    MouseState mouse_state;
    ViewTransform window_state;

    Window(enim::Settings& settings);
    ~Window();

    std::expected<void, enim::Error> load_image();
    std::expected<void, enim::Error> setup();
    void request_redraw();
    void poll_events();
    void draw();
    void run();

    // Event handling functions
    void dispatch_events(const RGFW_event& event);
    void on_key_pressed(const RGFW_event& event);
    void on_mouse_move(const RGFW_event& event);
    void on_mouse_wheel(const RGFW_event& event);
    void on_mouse_down(const RGFW_event& event);
    void on_mouse_up(const RGFW_event& event);
    void on_window_move(const RGFW_event& event);
    void on_resize(const RGFW_event& event);

    // helper functions
    Vec2i get_primary_monitor_size() const;
    void set_window_size(Vec2i window_size);
};

} // namespace enim
