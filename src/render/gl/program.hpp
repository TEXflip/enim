#pragma once

#include <expected>

#include <glad/glad.h>

#include <errors.hpp>

namespace enim {

std::expected<GLuint, enim::Error> compile_shader(const char* source, GLenum type);

/// @brief Compiles and links a vertex + fragment program.
std::expected<GLuint, enim::Error> link_program(const char* vert_source, const char* frag_source);

} // namespace enim
