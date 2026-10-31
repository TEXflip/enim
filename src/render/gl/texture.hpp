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
    int mip_levels_ready = 1;   // levels 0..mip_levels_ready-1 hold valid data

    Texture() = default;
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
    Texture(Texture&& other) noexcept { *this = std::move(other); }
    Texture& operator=(Texture&& other) noexcept {
        std::swap(this->id, other.id);
        this->size = other.size;
        this->format = other.format;
        this->mip_levels_ready = other.mip_levels_ready;
        return *this;
    }
    ~Texture() { glDeleteTextures(1, &this->id); }

    /// @brief Creates the texture object with the display sampling state (filters, wrap, swizzle). Storage is not allocated.
    static Texture create(const TextureFormat& format, Vec2i size);

    /// @brief Allocates level 0 storage with undefined contents, for textures filled by rendering into them.
    void allocate();

    /// @brief Allocates levels 1..N with undefined contents. Call it before level 0 gets its data: adding levels to a
    /// texture that already holds data makes the driver move it into new storage, a long stall for a large image.
    void allocate_mip_levels();

    /// @brief Number of levels of the full mip chain, down to 1x1.
    int mip_level_count() const;
    Vec2i level_size(int level) const;

    /// @brief Samples the ready levels with trilinear minification, or level 0 only with bilinear.
    /// Levels being built are never sampled. A disabled chain is kept, so enabling it again is instant.
    void set_mipmaps(bool enabled);

    /// @brief Clears level 0 to transparent black: with a framebuffer if the format is renderable, else by uploading zeros.
    void clear_transparent();

    /// @brief Generates the whole chain at once with glGenerateMipmap (blocks; fallback for non-renderable formats).
    void generate_mip_chain();
};

} // namespace enim
