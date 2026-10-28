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
    case FastStrategy::GpuConvert:
        return this->start_gpu_convert();
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

    this->texture.set_mipmaps(this->mipmaps);
    return {};
}

std::expected<void, enim::Error> FastRenderer::start_gpu_convert() {
    const ImageInfo& info = this->source->info();
    const PixelLayout& layout = info.layout;

    if (this->source->mapped_pixels().empty())
        return std::unexpected(enim::Error{enim::ErrorCode::ImageLoadError, "Failed to get image buffer"});

    auto job = std::make_unique<GpuConvertJob>();
    job->rows_per_band = std::min(this->plan.band_bytes / layout.stride, info.height);
    if (job->rows_per_band == 0)
        return std::unexpected(enim::Error{enim::ErrorCode::ImageLoadError, "A single image row exceeds the GPU convert staging buffer"});

    Vec2i size{static_cast<std::int32_t>(info.width), static_cast<std::int32_t>(info.height)};
    auto pass_result = job->pass.init_raw(this->plan.texture, size, this->plan.display);
    if (!pass_result)
        return pass_result;
    job->band.allocate(job->rows_per_band * layout.stride);
    job->start = std::chrono::steady_clock::now();

    PLOG_INFO << "Image size (WxH): " << info.width << "x" << info.height << ", " << static_cast<int>(layout.channels)
              << " stored / " << static_cast<int>(this->plan.texture.channels) << " texture channels, "
              << (info.height + job->rows_per_band - 1) / job->rows_per_band << " band(s) of " << job->rows_per_band << " rows";

    this->flip_y = false; // the convert pass writes top-down
    this->convert_job = std::move(job);
    return {};
}

void FastRenderer::convert_next_band() {
    GpuConvertJob& job = *this->convert_job;
    const ImageInfo& info = this->source->info();
    const std::size_t stride = info.layout.stride;

    std::size_t rows = std::min(job.rows_per_band, info.height - job.next_row);
    std::span<const std::uint8_t> bytes = this->source->mapped_pixels().subspan(job.next_row * stride, rows * stride);

    job.band.upload(0, bytes);
    job.pass.from_raw_band(job.band, info.layout, job.next_row, rows);
    job.next_row += rows;

    if (GLenum err = glGetError(); err != GL_NO_ERROR)
        PLOG_ERROR << "GL error " << err << " in GPU convert band at row " << job.next_row - rows;

    if (job.next_row < info.height)
        return;

    this->texture = job.pass.finish();
    this->texture.set_mipmaps(this->mipmaps);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - job.start);
    PLOG_INFO << "GPU convert done in " << elapsed.count() << " ms";
    this->convert_job.reset();
}

void FastRenderer::update(const ViewTransform& view) {
    if (this->convert_job)
        this->convert_next_band();
}

void FastRenderer::draw(const ViewTransform& view) {
    const Texture& current = this->convert_job ? this->convert_job->pass.target() : this->texture;
    this->image_pass.draw(current, view, this->flip_y);
}

bool FastRenderer::toggle_mipmaps() {
    this->mipmaps = !this->mipmaps;
    // while converting, the texture is still incomplete: the setting is applied when the last band is done
    if (!this->convert_job) {
        auto start = std::chrono::steady_clock::now();
        this->texture.set_mipmaps(this->mipmaps);
        if (this->mipmaps) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
            PLOG_INFO << "Mipmaps enabled (" << elapsed.count() << " ms)";
        }
    }
    if (!this->mipmaps)
        PLOG_INFO << "Mipmaps disabled";
    return true;
}

Vec2f FastRenderer::image_size() const {
    const ImageInfo& info = this->source->info();
    return Vec2f{static_cast<float>(info.width), static_cast<float>(info.height)};
}

} // namespace enim
