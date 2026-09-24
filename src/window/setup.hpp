#pragma once

#include <expected>

#include "../shaders/shaders.hpp"
#include "rglLoad.h"

/// @brief Retrieves the information log message for a given OpenGL shader or program.
/// @param logfunction The OpenGL function used to get the info log (e.g., glGetShaderInfoLog or glGetProgramInfoLog).
/// @param id The ID of the shader or program.
/// @return The info log message as a string.
std::string gl_get_infolog_message(void (*logfunction)(GLuint, GLsizei, GLsizei*, GLchar*), GLuint id) {
    GLchar message[1024];
    GLsizei message_size = 0;
    logfunction(id, sizeof(message), &message_size, message);
    std::string program_message(message, message_size);
    return program_message;
}

/// @brief Creates an OpenGL shader from a given source file.
/// @param shader_path The path to the shader source file.
/// @param type The type of the shader (e.g., GL_VERTEX_SHADER, GL_FRAGMENT_SHADER).
/// @return The compiled shader ID on success, or an error on failure.
std::expected<GLuint, enim::Error> create_shader(const char* shader_source_ptr, GLenum type) {
    // std::ifstream shader_file(shader_path);
    // if (!shader_file.is_open()) {
    //     return std::unexpected(enim::Error{enim::ErrorCode::FileOpenError, "could not open shader file"});
    // }
    // std::string shader_source((std::istreambuf_iterator<char>(shader_file)), std::istreambuf_iterator<char>());
    // const char* shader_source_ptr = shader_source.c_str();

    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &shader_source_ptr, NULL);
    glCompileShader(shader);

    GLint ec = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ec);

    if (!ec) {
        std::string msg = gl_get_infolog_message(glGetShaderInfoLog, shader);
        return std::unexpected(enim::Error{enim::ErrorCode::ShaderCompileError, "could not compile shader: " + msg});
    }

    return shader;
}

/// @brief Sets up the OpenGL context by compiling and linking the necessary shaders.
/// @return An empty result on success, or an error on failure.
std::expected<std::tuple<GLuint, std::uint32_t, std::uint32_t, std::uint32_t>, enim::Error> setup_main_glprogram() {
    auto frag_shader_result = create_shader(main_frag_shader, GL_FRAGMENT_SHADER);
    if (!frag_shader_result) {
        return std::unexpected(frag_shader_result.error());
    }
    GLuint frag_shader = frag_shader_result.value();

    auto vert_shader_result = create_shader(main_vert_shader, GL_VERTEX_SHADER);
    if (!vert_shader_result) {
        return std::unexpected(vert_shader_result.error());
    }
    GLuint vert_shader = vert_shader_result.value();

    GLuint main_program = glCreateProgram();

    glAttachShader(main_program, frag_shader);
    glAttachShader(main_program, vert_shader);
    glLinkProgram(main_program);

    GLint ec = 0;
    glGetProgramiv(main_program, GL_LINK_STATUS, &ec);
    if (!ec) {
        std::string msg = gl_get_infolog_message(glGetProgramInfoLog, main_program);
        return std::unexpected(enim::Error{enim::ErrorCode::ShaderLinkError, "could not link main program: " + msg});
    }
    glDeleteShader(frag_shader);
    glDeleteShader(vert_shader);

    float vertices[] = {
        -1.0f, -1.0f,
         1.0f, -1.0f,
        -1.0f,  1.0f,
         1.0f,  1.0f
    };

    std::uint32_t indices[] = {
        0, 1, 2,
        2, 1, 3
    };

    std::uint32_t vao, vbo, ebo;

    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);

    glBindVertexArray(vao);

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    return std::make_tuple(main_program, vao, vbo, ebo);
}

void keyfunc(const RGFW_event* event) {
    if (event->key.value == RGFW_keyEscape) {
        RGFW_window_setShouldClose(event->common.win, 1);
    }
}

struct WindowData {
    RGFW_window* win;
    GLuint main_program;
    std::uint32_t vao;
    std::uint32_t vbo;
    std::uint32_t ebo;
};

std::expected<WindowData, enim::Error> setup_window() {

    RGFW_init("enim", RGFW_initOpenGL);

    RGFW_glHints* hints = RGFW_getGlobalHints_OpenGL();
    hints->major = 3;
    hints->minor = 3;
    RGFW_setGlobalHints_OpenGL(hints);

    RGFW_windowFlags flags = RGFW_windowAllowDND | RGFW_windowCenter | RGFW_windowScaleToMonitor | RGFW_windowOpenGL;
    RGFW_window* win = RGFW_createWindow("enim", 0, 0, 800, 600, flags);
    if (!win) {
        return std::unexpected(enim::Error{enim::ErrorCode::WindowCreationError, "could not create window"});
    }

    RGFW_setEventCallback(RGFW_keyPressed, keyfunc);

    RGFW_window_makeCurrentContext_OpenGL(win);

    if (RGL_loadGL3((RGLloadfunc)RGFW_getProcAddress_OpenGL)) {
        return std::unexpected(enim::Error{enim::ErrorCode::WindowCreationError, "Failed to initialize GLAD"});
    }

    auto opengl_result = setup_main_glprogram();
    if (!opengl_result) {
        return std::unexpected(opengl_result.error());
    }
    auto [main_program, vao, vbo, ebo] = opengl_result.value();

    glClearColor(0.0431f, 0.0431f, 0.0431f, 1.0f);

    return WindowData{win, main_program, vao, vbo, ebo};
}