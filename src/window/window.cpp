#ifndef RGFW_OPENGL
#define RGFW_OPENGL
#endif

#include <chrono>

#include <RGFW.h>

#include "../dispatcher.hpp"
#include "../errors.hpp"
#include "../image_view.hpp"
#include "../logging.hpp"
#include "gl_debug_helpers.hpp"
#include "setup.hpp"
#include <cmath>
#include <plog/Log.h>
#include <settings/settings.hpp>
#include <window/window.hpp>

namespace enim {

#ifdef ENIM_OS_WIN
/// @brief Subclass procedure for handling window messages, specifically WM_SIZE for window resize events.
/// @param hwnd Handle to the window.
/// @param msg The message code.
/// @param wp Additional message information (WPARAM).
/// @param lp Additional message information (LPARAM).
/// @param Unused parameter (UINT_PTR).
/// @param ref Pointer to the Window instance.
/// @return The result of the default window procedure.
LRESULT CALLBACK SubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR ref);
#endif

Window::Window(enim::Settings& settings) : win(nullptr), main_program(0), vao(0), vbo(0), ebo(0), settings(settings) {}

Window::~Window() {
    glDeleteVertexArrays(1, &this->vao);
    glDeleteBuffers(1, &this->vbo);
    glDeleteBuffers(1, &this->ebo);
    glDeleteProgram(this->main_program);

    if (this->win != nullptr) {
        RGFW_window_close(this->win);
        RGFW_deinit();
    }
}

std::expected<void, enim::Error> Window::load_image() {
    if (this->settings.image_path.empty())
        return std::unexpected(enim::Error{enim::ErrorCode::ImageLoadError, "image path is empty"});

    auto view_result = enim::dispatch(this->settings);
    if (!view_result)
        return std::unexpected(view_result.error());

    this->image_view = std::move(view_result.value());
    this->window_state.image_size =
        Vec2f{static_cast<float>(this->image_view->reader().get_shape().width), static_cast<float>(this->image_view->reader().get_shape().height)};

    return std::expected<void, enim::Error>{};
}

void Window::request_redraw() { this->draw(); }

void Window::create_texture() {
    glGenTextures(1, &this->main_image_texture);
    glBindTexture(GL_TEXTURE_2D, this->main_image_texture);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    GLfloat borderColor[] = {0.0431f, 0.0431f, 0.0431f, 1.0f}; // Red border
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    // DEBUG: Log texture parameters
    auto shape = this->image_view->reader().get_shape();
    auto buffer = this->image_view->reader().get_buffer_mmap();
    if (!buffer) {
        PLOG_ERROR << "Failed to get image buffer";
        return;
    }
    GLint max_tex_size;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_tex_size);

    PLOG_INFO << "Image size (HxWxC): " << shape.height << "x" << shape.width << "x" << shape.channels;
    PLOG_INFO << "Max texture size: " << max_tex_size << " pixels per side";

    // GLint format = this->image_view->reader().get_shape().channels == 4 ? GL_RGBA : GL_RGB;

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, shape.width, shape.height, 0, GL_BGR, GL_UNSIGNED_BYTE, buffer);

    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        PLOG_ERROR << "GL Error in glTexImage2D: " << err;
    }
}

std::expected<void, enim::Error> Window::setup() {
    RGFW_init("enim", RGFW_initOpenGL);

    RGFW_glHints* hints = RGFW_getGlobalHints_OpenGL();
    hints->major = 3;
    hints->minor = 3;
    hints->profile = RGFW_glCore;
    hints->debug = this->settings.debug ? RGFW_TRUE : RGFW_FALSE;
    RGFW_setGlobalHints_OpenGL(hints);

    Vec2i window_size = this->get_primary_monitor_size() / 2;
    this->window_state.window_size = Vec2f(window_size);
    Vec2i win_pos = this->get_primary_monitor_size() / 2;

    this->win = RGFW_createWindow("enim", win_pos.x, win_pos.y, window_size.x, window_size.y, RGFW_windowAllowDND | RGFW_windowOpenGL);
    if (!this->win) {
        return std::unexpected(enim::Error{enim::ErrorCode::WindowCreationError, "could not create window"});
    }

    RGFW_window_makeCurrentContext_OpenGL(this->win);

    if (!gladLoadGLLoader((GLADloadproc)RGFW_getProcAddress_OpenGL)) {
        return std::unexpected(enim::Error{enim::ErrorCode::WindowCreationError, "Failed to initialize GLAD"});
    }

    if (GLAD_GL_KHR_debug) {
        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glDebugMessageCallback(onGLDebug, NULL);
    } else if (GLAD_GL_ARB_debug_output) {
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS_ARB);
        glDebugMessageCallbackARB(onGLDebugARB, NULL);
    } else {
        PLOG_WARNING << "No debug output available; use GL_CHECK() with glGetError";
    }

    auto opengl_result = setup_main_glprogram();
    if (!opengl_result) {
        return std::unexpected(opengl_result.error());
    }
    std::tie(this->main_program, this->vao, this->vbo, this->ebo) = opengl_result.value();

    return std::expected<void, enim::Error>{};
}

void Window::dispatch_events(const RGFW_event& event) {
    // PLOG_INFO << "Dispatching event of type: " << std::to_string(event.type);
    switch (event.type) {
#ifndef ENIM_OS_WIN
    case RGFW_windowResized:
        this->on_resize(event);
        break;
#endif
    case RGFW_mouseMotion:
        this->on_mouse_move(event);
        break;
    case RGFW_mouseButtonPressed:
        this->on_mouse_down(event);
        break;
    case RGFW_mouseButtonReleased:
        this->on_mouse_up(event);
        break;
    case RGFW_keyPressed:
        this->on_key_pressed(event);
        break;
    case RGFW_mouseScroll:
        this->on_mouse_wheel(event);
        break;
    case RGFW_windowMoved:
        this->on_window_move(event);
        break;
    default:
        // PLOG_WARNING << "Unhandled event type: " << std::to_string(event.type);
        break;
    }
}

void Window::run() {
    if (this->image_view) {
        this->create_texture();

        Vec2f Is = this->window_state.image_size;
        float image_aspect_ratio = Is.x / Is.y;
        Vec2f Ms = Vec2f(this->get_primary_monitor_size());
        IScalar window_width = std::min((IScalar)(Ms.x), (IScalar)(Ms.y * image_aspect_ratio));
        IScalar window_height = (IScalar)(window_width / image_aspect_ratio);

        this->set_window_size({window_width, window_height});
        this->window_state.fit();
    }

    glClearColor(0.0431f, 0.0431f, 0.0431f, 1.0f);
    this->request_redraw();
    this->poll_events();
}

void Window::poll_events() {
#ifdef ENIM_OS_WIN
    HWND hwnd = (HWND)RGFW_window_getHWND(this->win);
    SetWindowSubclass(hwnd, SubclassProc, 1, reinterpret_cast<DWORD_PTR>(this));
#endif

    RGFW_event event;
    while (!RGFW_window_shouldClose(this->win)) {
        RGFW_waitForEvent(RGFW_eventWaitNext);
        while (RGFW_window_checkEvent(win, &event))
            this->dispatch_events(event);
    }
}

inline void Window::draw() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glUseProgram(this->main_program);
    glViewport(0, 0, (int)this->window_state.window_size.x, (int)this->window_state.window_size.y);

    if (this->main_image_texture > 0) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, this->main_image_texture);
    }

    Vec2f scale, offset;
    this->window_state.uniforms(scale, offset);

    glUniform1i(glGetUniformLocation(this->main_program, "image"), 0);
    glUniform2f(glGetUniformLocation(this->main_program, "offset"), offset.x, offset.y);
    glUniform2f(glGetUniformLocation(this->main_program, "scale"), scale.x, scale.y);

    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        PLOG_ERROR << "GL Error: " << err;
    }

    glBindVertexArray(this->vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    RGFW_window_swapBuffers_OpenGL(this->win);
    glFlush();
}

Vec2i Window::get_primary_monitor_size() const {
    RGFW_monitor* mon = RGFW_getPrimaryMonitor();
    return {mon->mode.w / 2, mon->mode.h / 2};
}

void Window::set_window_size(Vec2i window_size) {
    this->window_state.window_size = Vec2f(window_size);
    RGFW_window_resize(this->win, window_size.x, window_size.y);
}

} // namespace enim
