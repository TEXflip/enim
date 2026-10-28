#pragma once

#include <expected>
#include <utility>

#include <glad/glad.h>

#include <errors.hpp>
#include <render/gl/texture.hpp>

namespace enim {

/// @brief Owns a framebuffer object rendering into one level of a texture. Move-only.
struct Framebuffer {
    GLuint id = 0;

    Framebuffer() = default;
    Framebuffer(const Framebuffer&) = delete;
    Framebuffer& operator=(const Framebuffer&) = delete;
    Framebuffer(Framebuffer&& other) noexcept { *this = std::move(other); }
    Framebuffer& operator=(Framebuffer&& other) noexcept {
        std::swap(this->id, other.id);
        return *this;
    }
    ~Framebuffer() { glDeleteFramebuffers(1, &this->id); }

    /// @brief Creates the framebuffer with a texture level as its color attachment and checks that it is complete.
    static std::expected<Framebuffer, enim::Error> create(const Texture& texture, GLint level = 0);

    /// @brief Replaces the color attachment and checks that the framebuffer is complete.
    std::expected<void, enim::Error> attach(const Texture& texture, GLint level);
};

} // namespace enim
