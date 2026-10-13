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
#include <plog/Log.h>
#include <settings/settings.hpp>
#include <window/window.hpp>
#include <cmath>

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
    if (!view_result) {
        const auto& error = view_result.error();
        PLOG_ERROR << error.message;
        return std::unexpected(error);
    }

    this->image_view = std::move(view_result.value());

    return std::expected<void, enim::Error>{};
}

void Window::request_redraw() {
    if (!this->redraw.exchange(true))
        this->redraw.notify_one();
}

void Window::center_image(bool fit_scale) {
    if (!this->image_view)
        return;

    auto shape = this->image_view->reader().get_shape();
    this->window_state.offset.x = (float)shape.width / 2.0f;
    this->window_state.offset.y = (float)shape.height / 2.0f;

    // if (fit_scale) {
    //     this->window_state.scale = shape.width < shape.height ? static_cast<float>(shape.width) / static_cast<float>(this->window_state.size.x) : static_cast<float>(shape.height) / static_cast<float>(this->window_state.size.y);
    //     this->window_state.zoom = std::log(this->window_state.scale);
    // }
}

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
#ifdef DEBUG
    hints->debug = RGFW_TRUE;
#endif
    RGFW_setGlobalHints_OpenGL(hints);

    RGFW_windowFlags flags = RGFW_windowAllowDND | RGFW_windowScaleToMonitor | RGFW_windowOpenGL;

    int window_width = 600;
    int window_height = 600;
#ifdef DEBUG
    flags |= RGFW_windowCenter;
#else
    if (this->image_view) {
        auto shape = this->image_view->reader().get_shape();
        RGFW_monitor* mon = RGFW_getPrimaryMonitor();
        if (mon) {
            if (shape.width > mon->mode.w || shape.height > mon->mode.h)
            {
                flags |= RGFW_windowMaximize;
                // this->window_state.scale = shape.width < shape.height ? static_cast<float>(shape.width) / static_cast<float>(mon->mode.w) : static_cast<float>(shape.height) / static_cast<float>(mon->mode.h);
                // this->window_state.zoom = std::log(this->window_state.scale);
            }
        }
    }
#endif

    this->win = RGFW_createWindow("enim", 20, 20, window_width, window_height, flags);
    if (!this->win) {
        return std::unexpected(enim::Error{enim::ErrorCode::WindowCreationError, "could not create window"});
    }

    this->window_state.size.x = window_width;
    this->window_state.size.y = window_height;

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

    glClearColor(0.0431f, 0.0431f, 0.0431f, 1.0f);

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
    case RGFW_keyPressed:
        this->on_key_pressed(event);
        break;
    case RGFW_mouseMotion:
        this->on_mouse_move(event);
        break;
    case RGFW_mouseScroll:
        this->on_mouse_wheel(event);
        break;
    case RGFW_windowMoved:
        this->on_window_move(event);
        break;
    case RGFW_mouseButtonPressed:
        this->on_mouse_down(event);
        break;
    case RGFW_mouseButtonReleased:
        this->on_mouse_up(event);
        break;
    default:
        // PLOG_WARNING << "Unhandled event type: " << std::to_string(event.type);
        break;
    }
}

void Window::run() {
    if (this->image_view)
    {
        this->create_texture();
        this->center_image();
    }

#ifdef ENIM_OS_WIN
    HWND hwnd = (HWND)RGFW_window_getHWND(this->win);
    SetWindowSubclass(hwnd, SubclassProc, 1, reinterpret_cast<DWORD_PTR>(this));
#endif

    RGFW_window_makeCurrentContext_OpenGL(NULL); // free context from this thread

    std::jthread render_thread([this](std::stop_token st) { render_loop(st); });

    this->poll_events();
    // std::jthread poll_thread([this](std::stop_token st)

    render_thread.request_stop();
    this->redraw.store(true);
    this->redraw.notify_one();
}

void Window::poll_events() {
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
    RGFW_window_getSize(this->win, &this->window_state.size.x, &this->window_state.size.y);
    glViewport(0, 0, (int)this->window_state.size.x, (int)this->window_state.size.y);

    Shape shape{1,0,0,0};

    if (this->main_image_texture > 0) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, this->main_image_texture);
        shape = this->image_view->reader().get_shape();
    }

    Vec2f Ws((float)this->window_state.size.x, (float)this->window_state.size.y);
    Vec2f Is((float)shape.width, (float)shape.height);
    Vec2f scale_v = this->window_state.scale * Ws / Is;
    Vec2f offset = this->window_state.scale * this->window_state.offset / Is;

    glUniform1i(glGetUniformLocation(this->main_program, "image"), 0);
    glUniform2f(glGetUniformLocation(this->main_program, "offset"), offset.x, offset.y);
    glUniform2f(glGetUniformLocation(this->main_program, "scale"), scale_v.x, scale_v.y);

    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        PLOG_ERROR << "GL Error: " << err;
    }

    glBindVertexArray(this->vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    // glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

    RGFW_window_swapBuffers_OpenGL(this->win);
    glFlush();
}

void Window::render_loop(std::stop_token stop_token) {
    RGFW_window_makeCurrentContext_OpenGL(this->win);
    // RGFW_window_swapInterval_OpenGL(this->win, 1);

    while (!stop_token.stop_requested()) {
        this->draw();
        this->redraw.wait(false);
        this->redraw.store(false);
    }
}

} // namespace enim
