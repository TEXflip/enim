#pragma once

#include <utility>

#include <glad/glad.h>

#include <linalgebra.hpp>
#include <render/gl/texture_format.hpp>

namespace enim {

/// @brief Owns a GL_TEXTURE_2D. Move-only.
struct Texture {
    GLuint id = 0;
    Vec2i size{0, 0};
    TextureFormat format;

    Texture() = default;
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
    Texture(Texture&& other) noexcept { *this = std::move(other); }
    Texture& operator=(Texture&& other) noexcept {
        std::swap(this->id, other.id);
        this->size = other.size;
        this->format = other.format;
        return *this;
    }
    ~Texture() { glDeleteTextures(1, &this->id); }

    /// @brief Creates the texture object with the display sampling state (filters, wrap, swizzle). Storage is not allocated.
    static Texture create(const TextureFormat& format, Vec2i size);

    /// @brief Generates the full mip chain and enables trilinear minification.
    void generate_mipmaps();
};

} // namespace enim
