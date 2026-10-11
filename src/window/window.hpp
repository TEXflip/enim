#pragma once

#include <expected>
#include <glad/glad.h>
#include <cstdint>
#include <memory>
#include <settings/settings.hpp>
#include <errors.hpp>
#include <image_view.hpp>
#include <thread>
#include <semaphore>
#include <atomic>
#include <mutex>
#include <limits>

#ifdef ENIM_OS_WIN
    #include <windows.h>
    #include <commctrl.h>
#endif

struct RGFW_window;
union RGFW_event;

namespace enim {

void on_key_pressed(const RGFW_event* event);
void on_window_resized(const RGFW_event* event);

struct MouseState {
    float x = 0.0f;
    float y = 0.0f;
    bool left_button_down = false;
    bool right_button_down = false;

    float last_LBD_x = std::numeric_limits<float>::quiet_NaN();
    float last_LBD_y = std::numeric_limits<float>::quiet_NaN();
    float last_RBD_x = std::numeric_limits<float>::quiet_NaN();
    float last_RBD_y = std::numeric_limits<float>::quiet_NaN();
};

struct WindowState {
    float zoom = 1.0f;
    float offset_x = 0.0f;
    float offset_y = 0.0f;
    int size_x = 0;
    int size_y = 0;
};

struct Window {
    RGFW_window* win;
    GLuint main_program, main_image_texture = 0;
    std::uint32_t vao;
    std::uint32_t vbo;
    std::uint32_t ebo;

    std::unique_ptr<enim::ImageView> image_view = nullptr;
    Settings& settings;
    MouseState mouse_state;
    WindowState window_state;

    std::atomic<bool> redraw = false;

    Window(enim::Settings& settings);
    ~Window();

    std::expected<void, enim::Error> load_image();
    void create_texture();
    std::expected<void, enim::Error> setup();
    void request_redraw();
    void poll_events();
    void draw();
    void run();
    void render_loop(std::stop_token stop_token);

    // Event handling functions
    void dispatch_events(const RGFW_event& event);
    void on_key_pressed(const RGFW_event& event);
    void on_mouse_move(const RGFW_event& event);
    void on_mouse_wheel(const RGFW_event& event);
    void on_mouse_down(const RGFW_event& event);
    void on_mouse_up(const RGFW_event& event);
    void on_window_move(const RGFW_event& event);
    void on_resize(const RGFW_event& event);
};

} // namespace enim
