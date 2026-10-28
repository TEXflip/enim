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
    bool has_mip_chain = false; // levels 1..N have been generated

    Texture() = default;
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
    Texture(Texture&& other) noexcept { *this = std::move(other); }
    Texture& operator=(Texture&& other) noexcept {
        std::swap(this->id, other.id);
        this->size = other.size;
        this->format = other.format;
        this->has_mip_chain = other.has_mip_chain;
        return *this;
    }
    ~Texture() { glDeleteTextures(1, &this->id); }

    /// @brief Creates the texture object with the display sampling state (filters, wrap, swizzle). Storage is not allocated.
    static Texture create(const TextureFormat& format, Vec2i size);

    /// @brief Allocates level 0 storage with undefined contents, for textures filled by rendering into them.
    void allocate();

    /// @brief Enables trilinear minification, generating the mip chain the first time, or goes back to level 0 only.
    /// The texture contents must be complete. A disabled chain is kept, so enabling it again is instant.
    void set_mipmaps(bool enabled);
};

} // namespace enim
