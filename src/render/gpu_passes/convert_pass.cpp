#include "convert_pass.hpp"

#include <array>

#include <render/gl/program.hpp>
#include <render/shaders/shaders.hpp>

namespace enim {

ConvertPass::~ConvertPass() {
    glDeleteVertexArrays(1, &this->vao);
    glDeleteProgram(this->program);
}

std::expected<void, enim::Error> ConvertPass::init_raw(const TextureFormat& dst, Vec2i size, const DisplayParams& display) {
    auto program_result = link_program({fullscreen_vert_shader}, {glsl_version, raw_fetch_glsl, convert_glsl, convert_raw_frag_shader});
    if (!program_result)
        return std::unexpected(program_result.error());
    this->program = program_result.value();
    this->display = display;

    this->texture = Texture::create(dst, size);
    this->texture.allocate();

    auto fbo_result = Framebuffer::create(this->texture);
    if (!fbo_result)
        return std::unexpected(fbo_result.error());
    this->fbo = std::move(fbo_result.value());

    // rows not converted yet show the background
    constexpr std::array<GLfloat, 4> transparent{0, 0, 0, 0};
    glBindFramebuffer(GL_FRAMEBUFFER, this->fbo.id);
    glClearBufferfv(GL_COLOR, 0, transparent.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glGenVertexArrays(1, &this->vao);

    TexelChannels texel = texel_channels(dst, display);
    glUseProgram(this->program);
    glUniform1i(glGetUniformLocation(this->program, "raw"), 0);
    glUniform4i(glGetUniformLocation(this->program, "channel_map"), texel.source[0], texel.source[1], texel.source[2], texel.source[3]);
    glUniform4f(glGetUniformLocation(this->program, "constant_value"), texel.constant[0], texel.constant[1], texel.constant[2], texel.constant[3]);
    glUniform2f(glGetUniformLocation(this->program, "value_range"), display.range.x, display.range.y);

    return {};
}

void ConvertPass::from_raw_band(const RawBuffer& band, const PixelLayout& src, std::size_t first_row, std::size_t rows) {
    const auto height = static_cast<std::size_t>(this->texture.size.y);

    glUseProgram(this->program);
    glUniform1i(glGetUniformLocation(this->program, "sample_type"), static_cast<GLint>(src.sample));
    glUniform1i(glGetUniformLocation(this->program, "big_endian"), src.big_endian ? 1 : 0);
    glUniform1ui(glGetUniformLocation(this->program, "stride"), static_cast<GLuint>(src.stride));
    glUniform1ui(glGetUniformLocation(this->program, "pixel_bytes"), static_cast<GLuint>(src.bytes_per_pixel()));
    glUniform1i(glGetUniformLocation(this->program, "bottom_up"), src.rows == RowOrder::BottomUp ? 1 : 0);
    glUniform1i(glGetUniformLocation(this->program, "image_height"), static_cast<GLint>(height));
    glUniform1i(glGetUniformLocation(this->program, "band_first_row"), static_cast<GLint>(first_row));

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_BUFFER, band.texture());

    // the target is top-down: storage rows of a bottom-up source land at the other end
    std::size_t target_row = src.rows == RowOrder::BottomUp ? height - first_row - rows : first_row;

    std::array<GLint, 4> viewport{};
    glGetIntegerv(GL_VIEWPORT, viewport.data());

    glBindFramebuffer(GL_FRAMEBUFFER, this->fbo.id);
    glViewport(0, static_cast<GLint>(target_row), this->texture.size.x, static_cast<GLsizei>(rows));
    glBindVertexArray(this->vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    glBindTexture(GL_TEXTURE_BUFFER, 0);
}

Texture ConvertPass::finish() {
    this->fbo = Framebuffer{};
    glDeleteProgram(this->program);
    this->program = 0;
    return std::move(this->texture);
}

} // namespace enim
