#include "image_pass.hpp"

#include <render/gl/program.hpp>
#include <render/shaders/shaders.hpp>

namespace enim {

ImagePass::~ImagePass() {
    glDeleteVertexArrays(1, &this->vao);
    glDeleteBuffers(1, &this->vbo);
    glDeleteProgram(this->program);
}

std::expected<void, enim::Error> ImagePass::init() {
    auto program_result = link_program({image_vert_shader}, {image_frag_shader});
    if (!program_result)
        return std::unexpected(program_result.error());
    this->program = program_result.value();

    float vertices[] = {
        // Position      // UV
        -1.0f, -1.0f,     -0.5f, -0.5f,
        1.0f, -1.0f,     0.5f, -0.5f,
        1.0f,  1.0f,     0.5f, 0.5f,

        -1.0f, -1.0f,     -0.5f, -0.5f,
        1.0f,  1.0f,     0.5f, 0.5f,
        -1.0f,  1.0f,     -0.5f, 0.5f
    };

    glGenVertexArrays(1, &this->vao);
    glGenBuffers(1, &this->vbo);

    glBindVertexArray(this->vao);

    glBindBuffer(GL_ARRAY_BUFFER, this->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    // Position: location 0, two floats.
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // UV: location 1, two floats.
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    return {};
}

void ImagePass::draw(const Texture& texture, const ViewTransform& view, bool flip_y) const {
    glUseProgram(this->program);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture.id);

    Vec2f scale, offset;
    view.uniforms(scale, offset);

    glUniform1i(glGetUniformLocation(this->program, "image"), 0);
    glUniform2f(glGetUniformLocation(this->program, "offset"), offset.x, offset.y);
    glUniform2f(glGetUniformLocation(this->program, "scale"), scale.x, scale.y);
    glUniform1i(glGetUniformLocation(this->program, "flip_y"), flip_y ? 1 : 0);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glBindVertexArray(this->vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    glDisable(GL_BLEND);
}

} // namespace enim
