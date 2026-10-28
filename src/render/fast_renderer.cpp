#include "fast_renderer.hpp"

#include <string>

#include <plog/Log.h>

namespace {

// destination texels blitted per update() while building mipmaps: ~16 MB written at RGBA8
constexpr std::size_t kMipmapTexelsPerStep = std::size_t{4} << 20;

} // namespace

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
    // storage and render target setup happen while the texture is empty: done later, on a texture holding the image,
    // the driver relocates or re-lays out all of it, which stalls when mipmaps are enabled
    this->texture.allocate();
    this->texture.allocate_mip_levels();
    this->prepare_mip_builder();

    auto start = std::chrono::steady_clock::now();
    glPixelStorei(GL_UNPACK_ALIGNMENT, params->alignment);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, params->row_length);
    glPixelStorei(GL_UNPACK_SWAP_BYTES, params->swap_bytes ? GL_TRUE : GL_FALSE);

    glBindTexture(GL_TEXTURE_2D, this->texture.id);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, size.x, size.y, params->format, params->type, pixels.data());

    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_SWAP_BYTES, GL_FALSE);

    if (GLenum err = glGetError(); err != GL_NO_ERROR)
        return std::unexpected(enim::Error{enim::ErrorCode::ImageLoadError, "glTexSubImage2D failed with GL error " + std::to_string(err)});
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
    PLOG_INFO << "Direct upload done in " << elapsed.count() << " ms";

    this->apply_mipmaps();
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
    this->prepare_mip_builder(); // cheap: the texture already is a render target
    this->apply_mipmaps();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - job.start);
    PLOG_INFO << "GPU convert done in " << elapsed.count() << " ms";
    this->convert_job.reset();
}

void FastRenderer::prepare_mip_builder() {
    auto init_result = this->mip_builder.init(this->texture);
    this->mip_blit = init_result.has_value();
    if (!init_result)
        PLOG_DEBUG << "Mipmaps can't be blitted (" << init_result.error().message << "), glGenerateMipmap will be used";
}

void FastRenderer::apply_mipmaps() {
    if (!this->mipmaps) {
        this->mip_job.reset();
    } else if (!this->mip_job && this->texture.mip_levels_ready < this->texture.mip_level_count()) {
        if (this->mip_blit) {
            this->mip_job = std::make_unique<MipJob>();
            this->mip_job->start = std::chrono::steady_clock::now();
        } else {
            PLOG_WARNING << "Texture format can't be blitted, generating mipmaps at once";
            this->texture.generate_mip_chain();
        }
    }
    this->texture.set_mipmaps(this->mipmaps);
}

void FastRenderer::build_next_mip_step() {
    MipJob& job = *this->mip_job;
    auto now = std::chrono::steady_clock::now();
    auto frame = std::chrono::duration_cast<std::chrono::microseconds>(now - (job.steps ? job.last_step : job.start));
    bool done = this->mip_builder.step(this->texture, kMipmapTexelsPerStep);
    ++job.steps;
    job.last_step = now;
    PLOG_DEBUG << "Mipmap step " << job.steps << ": " << this->texture.mip_levels_ready << " levels ready, "
               << frame.count() / 1000.0 << " ms since the previous step";
    // levels become visible as soon as they are complete
    this->texture.set_mipmaps(this->mipmaps);

    if (GLenum err = glGetError(); err != GL_NO_ERROR)
        PLOG_ERROR << "GL error " << err << " while building mipmaps";

    if (!done)
        return;
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - job.start);
    PLOG_INFO << "Mipmaps built: " << this->texture.mip_level_count() << " levels in " << job.steps << " steps, " << elapsed.count() << " ms";
    this->mip_job.reset();
}

void FastRenderer::update(const ViewTransform& view) {
    if (this->convert_job)
        this->convert_next_band();
    else if (this->mip_job)
        this->build_next_mip_step();
}

void FastRenderer::draw(const ViewTransform& view) {
    const Texture& current = this->convert_job ? this->convert_job->pass.target() : this->texture;
    this->image_pass.draw(current, view, this->flip_y);
}

bool FastRenderer::toggle_mipmaps() {
    this->mipmaps = !this->mipmaps;
    PLOG_INFO << "Mipmaps " << (this->mipmaps ? "enabled" : "disabled");
    // while converting, the texture is still incomplete: the setting is applied when the last band is done
    if (!this->convert_job)
        this->apply_mipmaps();
    return true;
}

Vec2f FastRenderer::image_size() const {
    const ImageInfo& info = this->source->info();
    return Vec2f{static_cast<float>(info.width), static_cast<float>(info.height)};
}

} // namespace enim
