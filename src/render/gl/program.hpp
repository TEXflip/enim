#pragma once

#include <expected>
#include <initializer_list>

#include <glad/glad.h>

#include <errors.hpp>

namespace enim {

/// @brief Compiles a shader from sources concatenated in order (the first one starts with #version).
std::expected<GLuint, enim::Error> compile_shader(std::initializer_list<const char*> sources, GLenum type);

/// @brief Compiles and links a vertex + fragment program.
std::expected<GLuint, enim::Error> link_program(std::initializer_list<const char*> vert_sources, std::initializer_list<const char*> frag_sources);

} // namespace enim
