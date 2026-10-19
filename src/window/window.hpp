#pragma once

#include <expected>
#include <glad/glad.h>
#include <cstdint>
#include <memory>
#include <settings/settings.hpp>
#include <errors.hpp>
#include <image_view.hpp>
#include <semaphore>
#include <limits>
#include <linalgebra.hpp>
#include <algorithm>


#ifdef ENIM_OS_WIN
    #include <windows.h>
    #include <commctrl.h>
#endif

struct RGFW_window;
union RGFW_event;

namespace enim {

struct ViewTransform {
    Vec2f window_size{1, 1};   // Ws, screen px
    Vec2f image_size{1, 1};    // Is, image px
    Vec2f center{0, 0};        // image px at the window center
    float zoom = 1.0f;        // screen px per image px

    Vec2f screen_to_centered(Vec2f s) const { return s - window_size * 0.5f; }
    Vec2f centered_to_image(Vec2f d)  const { return center + d / zoom; }
    Vec2f image_to_uv(Vec2f p)        const { return p / image_size; }

    Vec2f screen_to_image(Vec2f s) const { return centered_to_image(screen_to_centered(s)); }
    Vec2f image_to_screen(Vec2f p) const { return (p - center) * zoom + window_size * 0.5f; }

    void pan(Vec2f screen_delta) { center -= screen_delta / zoom; }

    void zoom_at(Vec2f screen_pos, float factor) {
        Vec2f d = screen_to_centered(screen_pos);
        Vec2f p = centered_to_image(d);      // image point under the cursor
        zoom *= factor;
        float min_zoom = std::min(16 / image_size.x, 16 / image_size.y); // the image is big at least 16 pixels in the window
        float max_zoom = std::min(window_size.x * 0.5, window_size.y * 0.5); // at least 2 pixels of the image are visible in the window
        zoom = std::min(std::max(zoom, min_zoom), max_zoom);
        center = p - d / zoom;
    }

    void fit() {
        zoom = std::min(window_size.x / image_size.x, window_size.y / image_size.y);
        center = image_size * 0.5f;
    }

    // shader uniforms
    void uniforms(Vec2f& scale, Vec2f& offset) const {
        scale  = window_size / (zoom * image_size);
        scale.y = -scale.y;
        offset = center / image_size;
    }
};

struct MouseState {
    Vec2f position{0.0f, 0.0f};
    bool left_button_down = false;
    bool right_button_down = false;

    Vec2f last_LBD_position{std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN()};
    Vec2f last_RBD_position{std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN()};
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
    ViewTransform window_state;

    Window(enim::Settings& settings);
    ~Window();

    std::expected<void, enim::Error> load_image();
    void create_texture();
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
};

} // namespace enim
