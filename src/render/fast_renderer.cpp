#include "fast_renderer.hpp"

#include <string>

#include <plog/Log.h>

namespace enim {

std::expected<void, enim::Error> FastRenderer::init() {
    auto pass_result = this->image_pass.init();
    if (!pass_result)
        return pass_result;

    switch (this->plan.fast) {
    case FastStrategy::DirectUpload:
        return this->upload_direct();
    default:
        return std::unexpected(enim::Error{enim::ErrorCode::ImageLoadError,
                                           std::string("Fast mode strategy not implemented yet: ") + to_string(this->plan.fast)});
    }
}

std::expected<void, enim::Error> FastRenderer::upload_direct() {
    const ImageInfo& info = this->source->info();
    auto params = direct_upload_params(info.layout, info.width, this->plan.texture, this->plan.display);
    if (!params)
        return std::unexpected(enim::Error{enim::ErrorCode::ImageLoadError, "Image layout can't be uploaded directly"});

    std::span<const std::uint8_t> pixels = this->source->mapped_pixels();
    if (pixels.empty())
        return std::unexpected(enim::Error{enim::ErrorCode::ImageLoadError, "Failed to get image buffer"});

    PLOG_INFO << "Image size (WxH): " << info.width << "x" << info.height << ", "
              << static_cast<int>(info.layout.channels) << " stored / " << static_cast<int>(this->plan.texture.channels) << " displayed channels";

    Vec2i size{static_cast<std::int32_t>(info.width), static_cast<std::int32_t>(info.height)};
    this->texture = Texture::create(this->plan.texture, size);
    this->flip_y = info.layout.rows == RowOrder::BottomUp;

    glPixelStorei(GL_UNPACK_ALIGNMENT, params->alignment);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, params->row_length);
    glPixelStorei(GL_UNPACK_SWAP_BYTES, params->swap_bytes ? GL_TRUE : GL_FALSE);

    glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(this->plan.texture.internal_format), size.x, size.y, 0,
                 params->format, params->type, pixels.data());

    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_SWAP_BYTES, GL_FALSE);

    if (GLenum err = glGetError(); err != GL_NO_ERROR)
        return std::unexpected(enim::Error{enim::ErrorCode::ImageLoadError, "glTexImage2D failed with GL error " + std::to_string(err)});

    this->texture.generate_mipmaps();
    return {};
}

void FastRenderer::draw(const ViewTransform& view) {
    this->image_pass.draw(this->texture, view, this->flip_y);
}

Vec2f FastRenderer::image_size() const {
    const ImageInfo& info = this->source->info();
    return Vec2f{static_cast<float>(info.width), static_cast<float>(info.height)};
}

} // namespace enim
