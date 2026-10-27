#include "program.hpp"

#include <string>

namespace {

std::string gl_get_infolog_message(void (*logfunction)(GLuint, GLsizei, GLsizei*, GLchar*), GLuint id) {
    GLchar message[1024];
    GLsizei message_size = 0;
    logfunction(id, sizeof(message), &message_size, message);
    return std::string(message, message_size);
}

} // namespace

namespace enim {

std::expected<GLuint, enim::Error> compile_shader(const char* source, GLenum type) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);

    GLint ec = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ec);

    if (!ec) {
        std::string msg = gl_get_infolog_message(glGetShaderInfoLog, shader);
        glDeleteShader(shader);
        return std::unexpected(enim::Error{enim::ErrorCode::ShaderCompileError, "could not compile shader: " + msg});
    }

    return shader;
}

std::expected<GLuint, enim::Error> link_program(const char* vert_source, const char* frag_source) {
    auto vert_result = compile_shader(vert_source, GL_VERTEX_SHADER);
    if (!vert_result)
        return std::unexpected(vert_result.error());

    auto frag_result = compile_shader(frag_source, GL_FRAGMENT_SHADER);
    if (!frag_result) {
        glDeleteShader(vert_result.value());
        return std::unexpected(frag_result.error());
    }

    GLuint program = glCreateProgram();
    glAttachShader(program, vert_result.value());
    glAttachShader(program, frag_result.value());
    glLinkProgram(program);
    glDeleteShader(vert_result.value());
    glDeleteShader(frag_result.value());

    GLint ec = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ec);
    if (!ec) {
        std::string msg = gl_get_infolog_message(glGetProgramInfoLog, program);
        glDeleteProgram(program);
        return std::unexpected(enim::Error{enim::ErrorCode::ShaderLinkError, "could not link program: " + msg});
    }

    return program;
}

} // namespace enim
