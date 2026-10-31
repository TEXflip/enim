#include "texture.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <vector>

#include <render/gl/framebuffer.hpp>

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

void Texture::clear_transparent() {
    if (auto fbo = Framebuffer::create(*this); fbo) {
        constexpr std::array<GLfloat, 4> transparent{0, 0, 0, 0};
        glBindFramebuffer(GL_FRAMEBUFFER, fbo->id);
        glClearBufferfv(GL_COLOR, 0, transparent.data());
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return;
    }
    constexpr std::size_t kChunkBytes = std::size_t{64} << 20;
    UploadParams params = texture_upload_params(this->format);
    const std::size_t row_bytes = static_cast<std::size_t>(this->size.x) * this->format.bytes_per_pixel();
    const std::size_t height = static_cast<std::size_t>(this->size.y);
    const std::size_t rows_per_chunk = std::max<std::size_t>(1, kChunkBytes / row_bytes);
    std::vector<std::uint8_t> zeros(std::min(rows_per_chunk, height) * row_bytes);
    glBindTexture(GL_TEXTURE_2D, this->id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, params.alignment);
    for (std::size_t y = 0; y < height; y += rows_per_chunk)
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, static_cast<GLint>(y), this->size.x,
                        static_cast<GLsizei>(std::min(rows_per_chunk, height - y)), params.format, params.type, zeros.data());
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
}

void Texture::generate_mip_chain() {
    glBindTexture(GL_TEXTURE_2D, this->id);
    // glGenerateMipmap stops at GL_TEXTURE_MAX_LEVEL
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, this->mip_level_count() - 1);
    glGenerateMipmap(GL_TEXTURE_2D);
    this->mip_levels_ready = this->mip_level_count();
}

} // namespace enim
