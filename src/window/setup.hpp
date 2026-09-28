#pragma once

#include <expected>
#include <glad/glad.h>

#include <cstdint>
#include <tuple>

#include "../errors.hpp"

std::expected<std::tuple<GLuint, std::uint32_t, std::uint32_t, std::uint32_t>, enim::Error> setup_main_glprogram();