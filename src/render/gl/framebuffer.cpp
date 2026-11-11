#include "framebuffer.hpp"

#include <string>

namespace enim {

std::expected<Framebuffer, enim::Error> Framebuffer::create(const Texture& texture, GLint level) {
    Framebuffer fbo;
    glGenFramebuffers(1, &fbo.id);
    auto attach_result = fbo.attach(texture, level);
    if (!attach_result)
        return std::unexpected(attach_result.error());
    return fbo;
}

std::expected<void, enim::Error> Framebuffer::attach(const Texture& texture, GLint level) {
    glBindFramebuffer(GL_FRAMEBUFFER, this->id);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture.id, level);
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (status != GL_FRAMEBUFFER_COMPLETE)
        return make_error(FramebufferError, "Framebuffer incomplete, status " + std::to_string(status));
    return {};
}

} // namespace enim
