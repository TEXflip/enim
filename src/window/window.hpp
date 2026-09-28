#pragma once

#include <expected>
#include <glad/glad.h>
#include <cstdint>
#include <memory>
#include <settings/settings.hpp>
#include <errors.hpp>
#include <image_view.hpp>

struct RGFW_window;
union RGFW_event;

namespace enim {

void on_key_pressed(const RGFW_event* event);
void on_window_resized(const RGFW_event* event);

struct Window {
    RGFW_window* win;
    GLuint main_program, main_image_texture = 0;
    std::uint32_t vao;
    std::uint32_t vbo;
    std::uint32_t ebo;

    std::unique_ptr<enim::ImageView> image_view = nullptr;
    enim::Settings& settings;

    Window(enim::Settings& settings);
    ~Window();

    std::expected<void, enim::Error> load_image();
    void create_texture();
    std::expected<void, enim::Error> setup();
    void run();

    // Event handling functions
    void dispatch_events(const RGFW_event& event);
    void on_key_pressed(const RGFW_event& event);
    void on_resize(const RGFW_event& event);
};

} // namespace enim
