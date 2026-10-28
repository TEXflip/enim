#include "framebuffer.hpp"

#include <string>

namespace enim {

std::expected<Framebuffer, enim::Error> Framebuffer::create(const Texture& texture) {
    Framebuffer fbo;
    glGenFramebuffers(1, &fbo.id);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo.id);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture.id, 0);
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (status != GL_FRAMEBUFFER_COMPLETE)
        return std::unexpected(enim::Error{enim::ErrorCode::ImageLoadError, "Framebuffer incomplete, status " + std::to_string(status)});
    return fbo;
}

} // namespace enim
