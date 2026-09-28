#ifndef RGFW_OPENGL
#define RGFW_OPENGL
#endif

#include <RGFW.h>

#include <window/window.hpp>
#include "../dispatcher.hpp"
#include "../image_view.hpp"
#include "../errors.hpp"
#include "../logging.hpp"
#include "setup.hpp"
#include "gl_debug_helpers.hpp"
#include <settings/settings.hpp>
#include <plog/Log.h>

namespace enim {

Window::Window(enim::Settings& settings) 
    : win(nullptr), main_program(0), vao(0), vbo(0), ebo(0), settings(settings) {}

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

std::expected<void, enim::Error> Window::load_image()
{
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

void Window::create_texture() {
    glGenTextures(1, &this->main_image_texture);
    glBindTexture(GL_TEXTURE_2D, this->main_image_texture);
    
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    GLfloat borderColor[] = { 1.0f, 0.0f, 0.0f, 1.0f }; // Red border
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);
    
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    
    // DEBUG: Log texture parameters
    auto shape = this->image_view->reader().get_shape();
    auto buffer = this->image_view->reader().get_buffer_mmap();
    GLint max_tex_size;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_tex_size);

    PLOG_INFO << "Texture dimensions: " << shape.width << "x" << shape.height;
    PLOG_INFO << "Max texture size: " << max_tex_size << " pixels per side";

    GLint format = this->image_view->reader().get_shape().channels == 4 ? GL_RGBA : GL_RGB;
    
    glTexImage2D(
        GL_TEXTURE_2D, 
        0, 
        format, 
        shape.width, 
        shape.height, 
        0, 
        format, 
        GL_UNSIGNED_BYTE, 
        buffer
    );
    
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
    hints->debug   = RGFW_TRUE;
#endif
    RGFW_setGlobalHints_OpenGL(hints);

    RGFW_windowFlags flags = RGFW_windowAllowDND | RGFW_windowScaleToMonitor | RGFW_windowOpenGL;

    int window_width = 800;
    int window_height = 600;
    if (this->image_view) {
        auto shape = this->image_view->reader().get_shape();
        RGFW_monitor* mon = RGFW_getPrimaryMonitor();
        if (mon) {
            if (shape.width > mon->mode.w || shape.height > mon->mode.h)
                flags |= RGFW_windowMaximize;
            window_width = shape.width;
            window_height = shape.height;
        }
    }

    this->win = RGFW_createWindow("enim", 0, 0, window_width, window_height, flags);
    if (!this->win) {
        return std::unexpected(enim::Error{enim::ErrorCode::WindowCreationError, "could not create window"});
    }

    // RGFW_setEventCallback(RGFW_keyPressed, on_key_pressed);
    // RGFW_setEventCallback(RGFW_windowResized, on_window_resized);

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

        // optional: silence low-priority notifications
        // glDebugMessageControlARB(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_LOW_ARB, 0, NULL, GL_FALSE);
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
    switch (event.type) {
        case RGFW_windowResized:
            this->on_resize(event);
            break;
        case RGFW_keyPressed:
            this->on_key_pressed(event);
            break;
        default:
            PLOG_WARNING << "Unhandled event type: " << event.type;
            break;
    }       
}

void Window::run() {
    if (this->image_view) 
        this->create_texture();
    
    RGFW_event event;

    while (!RGFW_window_shouldClose(this->win)) {
        while (RGFW_window_checkEvent(win, &event))
            this->dispatch_events(event);

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(this->main_program);

        if (this->main_image_texture > 0) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, this->main_image_texture);
        }
        
        glUniform1i(glGetUniformLocation(this->main_program, "image"), 0);

        GLenum err = glGetError();
        if (err != GL_NO_ERROR) {
            PLOG_ERROR << "GL Error: " << err;
        }
        
        glBindVertexArray(this->vao);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

        RGFW_window_swapBuffers_OpenGL(this->win);
        glFlush();
    }
}

} // namespace enim
