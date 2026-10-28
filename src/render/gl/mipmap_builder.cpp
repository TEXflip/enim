#include "mipmap_builder.hpp"

#include <algorithm>

namespace enim {

std::expected<void, enim::Error> MipmapBuilder::init(const Texture& texture) {
    // level 0 has the format of every level: if it is complete as a target, all levels are
    auto read_result = Framebuffer::create(texture, 0);
    if (!read_result)
        return std::unexpected(read_result.error());
    auto draw_result = Framebuffer::create(texture, 0);
    if (!draw_result)
        return std::unexpected(draw_result.error());

    this->read_fbo = std::move(read_result.value());
    this->draw_fbo = std::move(draw_result.value());
    this->next_row = 0;
    return {};
}

bool MipmapBuilder::step(Texture& texture, std::size_t texel_budget) {
    const int level_count = texture.mip_level_count();

    glEnable(GL_SCISSOR_TEST);
    while (texture.mip_levels_ready < level_count && texel_budget > 0) {
        const int level = texture.mip_levels_ready;
        const Vec2i src = texture.level_size(level - 1);
        const Vec2i dst = texture.level_size(level);

        // the levels were allocated with the texture (Texture::allocate_mip_levels), so the attachments are complete
        (void)this->read_fbo.attach(texture, level - 1);
        (void)this->draw_fbo.attach(texture, level);

        const auto row_texels = static_cast<std::size_t>(dst.x);
        const int rows = static_cast<int>(std::clamp<std::size_t>(texel_budget / row_texels, 1, dst.y - this->next_row));

        glBindFramebuffer(GL_READ_FRAMEBUFFER, this->read_fbo.id);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, this->draw_fbo.id);
        // the whole level is mapped and the scissor keeps the band: sampling is the same as one full blit, no seams
        glScissor(0, this->next_row, dst.x, rows);
        glBlitFramebuffer(0, 0, src.x, src.y, 0, 0, dst.x, dst.y, GL_COLOR_BUFFER_BIT, GL_LINEAR);

        this->next_row += rows;
        texel_budget -= std::min(texel_budget, rows * row_texels);
        if (this->next_row == dst.y) {
            this->next_row = 0;
            ++texture.mip_levels_ready;
        }
    }
    glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    return texture.mip_levels_ready == level_count;
}

} // namespace enim
