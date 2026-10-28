#include "texture.hpp"

#include <algorithm>
#include <bit>

namespace enim {

Texture Texture::create(const TextureFormat& format, Vec2i size) {
    Texture texture;
    texture.size = size;
    texture.format = format;

    glGenTextures(1, &texture.id);
    glBindTexture(GL_TEXTURE_2D, texture.id);

    // the image shader discards fragments outside the image, so edges never sample a border
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    // until the texture is complete only level 0 exists
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, format.swizzle.data());

    return texture;
}

namespace {

void allocate_level(const Texture& texture, int level) {
    Vec2i size = texture.level_size(level);
    // no data: format and type only need to be compatible with a non-integer internal format
    glTexImage2D(GL_TEXTURE_2D, level, static_cast<GLint>(texture.format.internal_format), size.x, size.y, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);
}

} // namespace

void Texture::allocate() {
    glBindTexture(GL_TEXTURE_2D, this->id);
    allocate_level(*this, 0);
}

void Texture::allocate_mip_levels() {
    glBindTexture(GL_TEXTURE_2D, this->id);
    for (int level = 1; level < this->mip_level_count(); ++level)
        allocate_level(*this, level);
}

int Texture::mip_level_count() const {
    return std::bit_width(static_cast<unsigned>(std::max(this->size.x, this->size.y)));
}

Vec2i Texture::level_size(int level) const {
    return {std::max(this->size.x >> level, 1), std::max(this->size.y >> level, 1)};
}

void Texture::set_mipmaps(bool enabled) {
    bool mipmapped = enabled && this->mip_levels_ready > 1;
    glBindTexture(GL_TEXTURE_2D, this->id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, mipmapped ? this->mip_levels_ready - 1 : 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mipmapped ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
}

void Texture::generate_mip_chain() {
    glBindTexture(GL_TEXTURE_2D, this->id);
    // glGenerateMipmap stops at GL_TEXTURE_MAX_LEVEL
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, this->mip_level_count() - 1);
    glGenerateMipmap(GL_TEXTURE_2D);
    this->mip_levels_ready = this->mip_level_count();
}

} // namespace enim
