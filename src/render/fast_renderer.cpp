#include "fast_renderer.hpp"

#include <algorithm>
#include <string>

#include <plog/Log.h>

#include "../logging.hpp"

namespace {

// destination texels blitted per update() while building mipmaps: ~16 MB written at RGBA8
constexpr std::size_t kMipmapTexelsPerStep = std::size_t{4} << 20;

// StreamUpload: converted bytes waiting in the queue (bounds the RAM ahead of the GPU), and uploaded per update()
constexpr std::size_t kStreamQueueBytes = std::size_t{64} << 20;
constexpr std::size_t kStreamUploadBytesPerStep = std::size_t{64} << 20;

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
    case FastStrategy::StreamUpload:
        return this->start_stream_upload();
    default:
        return make_error(ImageLoadError, std::string("Fast mode strategy not implemented yet: ") + to_string(this->plan.fast));
    }
}

std::expected<void, enim::Error> FastRenderer::upload_direct() {
    const ImageInfo& info = this->source->info();
    auto params = direct_upload_params(info.layout, info.width, this->plan.texture, this->plan.display);
    if (!params)
        return make_error(ImageLoadError, "Image layout can't be uploaded directly");

    std::span<const std::uint8_t> pixels = this->source->mapped_pixels();
    if (pixels.empty())
        return make_error(ImageLoadError, "Failed to get image buffer");

    PLOGI << "Image size (WxH): " << info.width << "x" << info.height << ", "
              << static_cast<int>(info.layout.channels) << " stored / " << static_cast<int>(this->plan.texture.channels) << " displayed channels";

    this->create_display_texture();
    Vec2i size = this->texture.size;
    this->flip_y = info.layout.rows == RowOrder::BottomUp;

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
        return make_error(GLError, "glTexSubImage2D failed with GL error " + std::to_string(err));
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
    PLOGI << "Direct upload done in " << elapsed.count() << " ms";
    PLOG_PROFILE << "direct upload done";

    this->apply_mipmaps();
    return {};
}

void FastRenderer::create_display_texture() {
    const ImageInfo& info = this->source->info();
    Vec2i size{static_cast<std::int32_t>(info.width), static_cast<std::int32_t>(info.height)};
    this->texture = Texture::create(this->plan.texture, size);
    // storage and render target setup happen while the texture is empty: done later, on a texture holding the image,
    // the driver relocates or re-lays out all of it, which stalls when mipmaps are built
    this->texture.allocate();
    this->texture.allocate_mip_levels();
    this->prepare_mip_builder();
}

std::expected<void, enim::Error> FastRenderer::start_stream_upload() {
    const ImageInfo& info = this->source->info();

    PLOGI << "Image size (WxH): " << info.width << "x" << info.height << ", " << static_cast<int>(info.layout.channels)
              << " stored / " << static_cast<int>(this->plan.texture.channels) << " texture channels, decoded on the CPU";

    this->create_display_texture();
    this->flip_y = false; // the sink writes top-down rows

    this->texture.clear_transparent(); // rows not decoded yet show the background

    auto job = std::make_unique<StreamJob>(PixelConverter(info.layout, info.width, this->plan.texture, this->plan.display),
                                           kStreamQueueBytes);
    job->start = std::chrono::steady_clock::now();
    job->thread = std::jthread([this, raw = job.get()](std::stop_token stop) {
        raw->sink = std::make_unique<TextureUploadSink>(this->source->info(), raw->converter, raw->queue, this->wakeup, stop);
        (void)this->source->decode(*raw->sink, stop); // the result reaches the queue through the sink's on_done
    });
    this->stream_job = std::move(job);
    return {};
}

void FastRenderer::upload_stream_bands() {
    StreamJob& job = *this->stream_job;
    UploadParams params = texture_upload_params(this->plan.texture);

    glBindTexture(GL_TEXTURE_2D, this->texture.id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, params.alignment);
    std::size_t uploaded_bytes = 0;
    const bool first_band = job.rows_uploaded == 0;
    while (uploaded_bytes < kStreamUploadBytesPerStep) {
        std::optional<UploadBand> band = job.queue.pop();
        if (!band)
            break;
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, static_cast<GLint>(band->first_row), this->texture.size.x,
                        static_cast<GLsizei>(band->rows), params.format, params.type, band->pixels.data());
        uploaded_bytes += band->pixels.size();
        job.rows_uploaded += band->rows;
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    if (GLenum err = glGetError(); err != GL_NO_ERROR)
        PLOGE << "GL error " << err << " while uploading decoded rows";
    if (first_band && job.rows_uploaded > 0)
        PLOG_PROFILE << "first band uploaded";

    std::optional<std::expected<void, enim::Error>> result = job.queue.take_result();
    if (!result)
        return;

    PLOG_PROFILE << "stream upload done";
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - job.start);
    if (*result)
        PLOGI << "Stream upload done in " << elapsed.count() << " ms";
    else
        PLOGE << "Decoding failed after " << job.rows_uploaded << " rows: " << result->error().message;

    this->stream_job.reset(); // the decode thread has returned, so this joins immediately
    // a partial image still gets its mipmaps: the missing rows stay transparent
    this->apply_mipmaps();
}

std::expected<void, enim::Error> FastRenderer::start_gpu_convert() {
    const ImageInfo& info = this->source->info();
    const PixelLayout& layout = info.layout;

    if (this->source->mapped_pixels().empty())
        return make_error(ImageLoadError, "Failed to get image buffer");

    auto job = std::make_unique<GpuConvertJob>();
    job->rows_per_band = std::min(this->plan.band_bytes / layout.stride, info.height);
    if (job->rows_per_band == 0)
        return make_error(ImageLoadError, "A single image row exceeds the GPU convert staging buffer");

    Vec2i size{static_cast<std::int32_t>(info.width), static_cast<std::int32_t>(info.height)};
    auto pass_result = job->pass.init_raw(this->plan.texture, size, this->plan.display);
    if (!pass_result)
        return pass_result;
    job->band.allocate(job->rows_per_band * layout.stride);
    job->start = std::chrono::steady_clock::now();

    PLOGI << "Image size (WxH): " << info.width << "x" << info.height << ", " << static_cast<int>(layout.channels)
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
        PLOGE << "GL error " << err << " in GPU convert band at row " << job.next_row - rows;
    if (job.next_row == rows)
        PLOG_PROFILE << "first band uploaded";

    if (job.next_row < info.height)
        return;

    this->texture = job.pass.finish();
    PLOG_PROFILE << "gpu convert done";
    this->prepare_mip_builder(); // cheap: the texture already is a render target
    this->apply_mipmaps();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - job.start);
    PLOGI << "GPU convert done in " << elapsed.count() << " ms";
    this->convert_job.reset();
}

void FastRenderer::prepare_mip_builder() {
    auto init_result = this->mip_builder.init(this->texture);
    this->mip_blit = init_result.has_value();
    if (!init_result)
        PLOGD << "Mipmaps can't be blitted (" << init_result.error().message << "), glGenerateMipmap will be used";
}

void FastRenderer::apply_mipmaps() {
    if (!this->mipmaps) {
        this->mip_job.reset();
    } else if (!this->mip_job && this->texture.mip_levels_ready < this->texture.mip_level_count()) {
        if (this->mip_blit) {
            this->mip_job = std::make_unique<MipJob>();
            this->mip_job->start = std::chrono::steady_clock::now();
        } else {
            PLOGW << "Texture format can't be blitted, generating mipmaps at once";
            this->texture.generate_mip_chain();
            PLOG_PROFILE << "mipmaps built";
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
    PLOGD << "Mipmap step " << job.steps << ": " << this->texture.mip_levels_ready << " levels ready, "
               << frame.count() / 1000.0 << " ms since the previous step";
    // levels become visible as soon as they are complete
    this->texture.set_mipmaps(this->mipmaps);

    if (GLenum err = glGetError(); err != GL_NO_ERROR)
        PLOGE << "GL error " << err << " while building mipmaps";

    if (!done)
        return;
    PLOG_PROFILE << "mipmaps built";
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - job.start);
    PLOGI << "Mipmaps built: " << this->texture.mip_level_count() << " levels in " << job.steps << " steps, " << elapsed.count() << " ms";
    this->mip_job.reset();
}

void FastRenderer::update(const ViewTransform& view) {
    if (this->convert_job)
        this->convert_next_band();
    else if (this->stream_job)
        this->upload_stream_bands();
    else if (this->mip_job)
        this->build_next_mip_step();
}

void FastRenderer::draw(const ViewTransform& view) {
    const Texture& current = this->convert_job ? this->convert_job->pass.target() : this->texture;
    this->image_pass.draw(current, view, this->flip_y);
}

bool FastRenderer::toggle_mipmaps() {
    this->mipmaps = !this->mipmaps;
    PLOGI << "Mipmaps " << (this->mipmaps ? "enabled" : "disabled");
    // while converting, the texture is still incomplete: the setting is applied when the last band is done
    if (!this->convert_job && !this->stream_job)
        this->apply_mipmaps();
    return true;
}

Vec2f FastRenderer::image_size() const {
    const ImageInfo& info = this->source->info();
    return Vec2f{static_cast<float>(info.width), static_cast<float>(info.height)};
}

} // namespace enim
