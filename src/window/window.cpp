#ifndef RGFW_OPENGL
#define RGFW_OPENGL
#endif

#include <chrono>

#include <RGFW.h>

#include "../errors.hpp"
#include "../logging.hpp"
#include "gl_debug_helpers.hpp"
#include <cmath>
#include <plan/loader.hpp>
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

Window::Window(enim::Settings& settings) : win(nullptr), settings(settings) {}

Window::~Window() {
    // the renderer frees GL resources, so it must go before the context
    this->renderer.reset();

    if (this->win != nullptr) {
        RGFW_window_close(this->win);
        RGFW_deinit();
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

    Vec2i window_size = {600, 600};
    this->window_state.window_size = Vec2f(window_size);

    RGFW_windowFlags window_flags = RGFW_windowAllowDND | RGFW_windowOpenGL | RGFW_windowCenter;
    this->win = RGFW_createWindow("enim", 0, 0, window_size.x, window_size.y, window_flags);
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

    return std::expected<void, enim::Error>{};
}


std::expected<void, enim::Error> Window::load_image() {
    if (this->settings.image_path.empty())
        return std::unexpected(enim::Error{enim::ErrorCode::ImageLoadError, "image path is empty"});
    
    auto renderer_result = enim::load(this->settings, enim::query_gpu_limits());
    if (!renderer_result)
        return std::unexpected(renderer_result.error());

    auto init_result = renderer_result.value()->init();
    if (!init_result)
        return std::unexpected(init_result.error());

    this->renderer = std::move(renderer_result.value());
    this->window_state.image_size = this->renderer->image_size();

    return std::expected<void, enim::Error>{};
}

void Window::request_redraw() { this->draw(); }

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
    if (this->renderer) {
        Vec2f Is = this->window_state.image_size;
        float image_aspect_ratio = Is.x / Is.y;
        Vec2f Ms = Vec2f(this->get_primary_monitor_size());

        float s_area = std::sqrt(Ms.x * Ms.y / (Is.x * Is.y));
        float s_max  = std::min(Ms.x / Is.x, Ms.y / Is.y);
        float s = std::min(s_area, s_max);

        this->set_window_size({std::round(s * Is.x), std::round(s * Is.y)});
        RGFW_window_center(this->win);
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
        bool loading = this->renderer && this->renderer->loading();
        RGFW_waitForEvent(loading ? RGFW_eventNoWait : RGFW_eventWaitNext);
        while (RGFW_window_checkEvent(win, &event))
            this->dispatch_events(event);
        // each redraw lets the renderer advance the load by one step
        if (loading)
            this->request_redraw();
    }
}

inline void Window::draw() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glViewport(0, 0, (int)this->window_state.window_size.x, (int)this->window_state.window_size.y);

    if (this->renderer) {
        this->renderer->update(this->window_state);
        this->renderer->draw(this->window_state);
    }

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
