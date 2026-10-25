#include "fast_renderer.hpp"

#include <plog/Log.h>
#include <window/setup.hpp>

namespace enim {

FastRenderer::~FastRenderer() {
    glDeleteTextures(1, &this->texture);
    glDeleteVertexArrays(1, &this->vao);
    glDeleteBuffers(1, &this->vbo);
    glDeleteBuffers(1, &this->ebo);
    glDeleteProgram(this->program);
}

std::expected<void, enim::Error> FastRenderer::init() {
    auto program_result = setup_main_glprogram();
    if (!program_result)
        return std::unexpected(program_result.error());
    std::tie(this->program, this->vao, this->vbo, this->ebo) = program_result.value();

    return this->create_texture();
}

std::expected<void, enim::Error> FastRenderer::create_texture() {
    glGenTextures(1, &this->texture);
    glBindTexture(GL_TEXTURE_2D, this->texture);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    GLfloat borderColor[] = {0.0431f, 0.0431f, 0.0431f, 1.0f};
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    auto shape = this->reader->get_shape();
    auto buffer = this->reader->get_buffer_mmap();
    if (!buffer)
        return std::unexpected(enim::Error{enim::ErrorCode::ImageLoadError, "Failed to get image buffer"});

    PLOG_INFO << "Image size (HxWxC): " << shape.height << "x" << shape.width << "x" << shape.channels;

    // GLint format = shape.channels == 4 ? GL_RGBA : GL_RGB;

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, shape.width, shape.height, 0, GL_BGR, GL_UNSIGNED_BYTE, buffer);

    GLenum err = glGetError();
    if (err != GL_NO_ERROR)
        PLOG_ERROR << "GL Error in glTexImage2D: " << err;

    return {};
}

void FastRenderer::draw(const ViewTransform& view) {
    glUseProgram(this->program);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, this->texture);

    Vec2f scale, offset;
    view.uniforms(scale, offset);

    glUniform1i(glGetUniformLocation(this->program, "image"), 0);
    glUniform2f(glGetUniformLocation(this->program, "offset"), offset.x, offset.y);
    glUniform2f(glGetUniformLocation(this->program, "scale"), scale.x, scale.y);

    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        PLOG_ERROR << "GL Error: " << err;
    }

    glBindVertexArray(this->vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

Vec2f FastRenderer::image_size() const {
    const Shape& shape = this->reader->get_shape();
    return Vec2f{static_cast<float>(shape.width), static_cast<float>(shape.height)};
}

} // namespace enim
