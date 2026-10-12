#include "setup.hpp"

#include <string>

#include "../shaders/shaders.hpp"

namespace {

std::string gl_get_infolog_message(void (*logfunction)(GLuint, GLsizei, GLsizei*, GLchar*), GLuint id) {
    GLchar message[1024];
    GLsizei message_size = 0;
    logfunction(id, sizeof(message), &message_size, message);
    return std::string(message, message_size);
}

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

} // namespace

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
        // Position      // UV
        -1.0f, -1.0f,     0.0f, 0.0f,
        1.0f, -1.0f,     1.0f, 0.0f,
        1.0f,  1.0f,     1.0f, 1.0f,

        -1.0f, -1.0f,     0.0f, 0.0f,
        1.0f,  1.0f,     1.0f, 1.0f,
        -1.0f,  1.0f,     0.0f, 1.0f
    };

    std::uint32_t vao, vbo, ebo;

    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);

    glBindVertexArray(vao);

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    // Position: location 0, two floats.
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // UV: location 1, two floats.
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    return std::make_tuple(main_program, vao, vbo, ebo);
}