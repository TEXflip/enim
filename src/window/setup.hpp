#pragma once

#include <expected>

#include "../shaders/shaders.hpp"
#include <glad/glad.h>

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