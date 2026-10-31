#include "tiled_renderer.hpp"

#include <algorithm>

#include <plog/Log.h>

#include <core/cache_dir.hpp>

namespace {

// frame bands waiting for the main thread, and uploaded per update()
constexpr std::size_t kFrameQueueBytes = std::size_t{64} << 20;
constexpr std::size_t kFrameUploadBytesPerStep = std::size_t{64} << 20;

// an incomplete frame is resampled at most this often while the pyramid is being built
constexpr auto kRefineInterval = std::chrono::milliseconds(100);
// preview passes: 1, 2, 4, 8, 16 level-0 samples per texel
constexpr int kMaxPreviewPass = 4;

} // namespace

namespace enim {

TiledRenderer::TiledRenderer(std::unique_ptr<ImageSource> source, const LoadPlan& plan, std::filesystem::path image_path,
                             std::filesystem::path cache_dir, std::size_t max_texture_size, std::function<void()> wakeup)
    : source(std::move(source)), plan(plan), image_path(std::move(image_path)), cache_dir(std::move(cache_dir)),
      max_texture_size(max_texture_size), wakeup(std::move(wakeup)), queue(kFrameQueueBytes) {}

TiledRenderer::~TiledRenderer() {
    // stop the workers before the store, the queue and the source go
    this->build_thread.request_stop();
    if (this->build_thread.joinable())
        this->build_thread.join();
    this->sampler.reset();

    // an interrupted build would be rebuilt anyway: don't leave a large incomplete file behind
    if (this->store && !this->store->complete()) {
        std::filesystem::path path = this->store->path();
        this->store.reset(); // unmapped: on Windows a mapped file can't be deleted
        std::error_code ec;
        if (std::filesystem::remove(path, ec))
            PLOG_INFO << "Removed the incomplete pyramid " << path.string();
    }
}

std::expected<void, enim::Error> TiledRenderer::init() {
    auto pass_result = this->image_pass.init();
    if (!pass_result)
        return pass_result;

    const ImageInfo& info = this->source->info();
    std::filesystem::path path = pyramid_path(this->cache_dir, cache_key(this->image_path, this->source->header_bytes()));
    auto store_result = PyramidStore::open_or_create(path, info);
    if (!store_result)
        return std::unexpected(store_result.error());
    this->store = std::move(store_result.value());

    const PyramidGeometry& geom = this->store->geometry();
    PLOG_INFO << "Image size (WxH): " << info.width << "x" << info.height << ", " << static_cast<int>(info.layout.channels)
              << " stored / " << static_cast<int>(this->plan.texture.channels) << " frame channels";
    PLOG_INFO << "Pyramid " << path.string() << ": " << geom.level_count() << " levels, " << (geom.file_size >> 20) << " MiB, "
              << (this->store->complete() ? "cached" : "building");

    std::span<const std::uint8_t> source_pixels;
    if (this->plan.level0 == Level0Source::SourceMmap)
        source_pixels = this->source->mapped_pixels();
    this->sampler = std::make_unique<ViewSampler>(*this->store, info, source_pixels, this->plan.texture, this->plan.display,
                                                  this->queue, this->wakeup);

    if (!this->store->complete()) {
        this->builder = std::make_unique<PyramidBuilderSink>(*this->store, info, this->wakeup);
        this->build_start = std::chrono::steady_clock::now();
        this->build_thread = std::jthread([this](std::stop_token stop) {
            (void)this->source->decode(*this->builder, stop); // the result reaches the builder through on_done
        });
    }
    return {};
}

void TiledRenderer::request_frame(const FramePlan& frame, int pass) {
    Vec2i size{static_cast<std::int32_t>(frame.rect.width()), static_cast<std::int32_t>(frame.rect.height())};

    Frame next;
    next.plan = frame;
    next.generation = this->next_generation++;
    next.pass = pass;
    next.progress = this->store->progress();
    if (this->pending && this->pending->texture.size.x == size.x && this->pending->texture.size.y == size.y) {
        next.texture = std::move(this->pending->texture);
    } else {
        next.texture = Texture::create(this->plan.texture, size);
        next.texture.allocate();
    }
    next.texture.clear_transparent(); // texels not sampled yet show the stale frame below
    this->pending = std::move(next);

    this->sampler->post(FrameRequest{this->pending->generation, frame, pass});
}

void TiledRenderer::upload_bands() {
    UploadParams params = texture_upload_params(this->plan.texture);
    bool bound = false;
    std::size_t uploaded_bytes = 0;
    while (uploaded_bytes < kFrameUploadBytesPerStep) {
        std::optional<UploadBand> band = this->queue.pop();
        if (!band)
            break;
        if (!this->pending || band->generation != this->pending->generation)
            continue; // replaced by a newer request
        if (band->end_of_frame) {
            this->pending->complete = band->complete;
            this->current = std::move(this->pending);
            this->pending.reset();
            continue;
        }
        if (!bound) {
            glPixelStorei(GL_UNPACK_ALIGNMENT, params.alignment);
            bound = true;
        }
        glBindTexture(GL_TEXTURE_2D, this->pending->texture.id);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, static_cast<GLint>(band->first_row), this->pending->texture.size.x,
                        static_cast<GLsizei>(band->rows), params.format, params.type, band->pixels.data());
        uploaded_bytes += band->pixels.size();
    }
    if (bound) {
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        if (GLenum err = glGetError(); err != GL_NO_ERROR)
            PLOG_ERROR << "GL error " << err << " while uploading a frame";
    }
}

void TiledRenderer::check_build() {
    if (!this->builder || !this->builder->finished())
        return;
    this->build_thread.join();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - this->build_start);
    const auto& result = this->builder->result();
    if (result && *result)
        PLOG_INFO << "Pyramid built in " << elapsed.count() << " ms";
    else if (result)
        PLOG_ERROR << "Pyramid build failed after " << elapsed.count() << " ms: " << result->error().message;
    this->builder.reset();
}

void TiledRenderer::update(const ViewTransform& view) {
    this->upload_bands();
    this->check_build();

    const PyramidGeometry& geom = this->store->geometry();
    const Frame* latest = this->pending ? &*this->pending : this->current ? &*this->current : nullptr;
    if (!latest || !covers(latest->plan, view, geom)) {
        this->request_frame(plan_frame(view, geom, this->max_texture_size), 0);
        return;
    }

    if (this->refine_due()) {
        this->last_refine = std::chrono::steady_clock::now();
        this->request_frame(this->current->plan, std::min(this->current->pass + 1, kMaxPreviewPass));
    }
}

bool TiledRenderer::refine_due() const {
    if (this->pending || !this->current || this->current->complete)
        return false;
    if (this->store->progress() == this->current->progress)
        return this->current->pass < kMaxPreviewPass;
    // the last rows (and the end of the build) are never deferred: nothing would wake the main thread for them
    return this->store->complete() || std::chrono::steady_clock::now() - this->last_refine >= kRefineInterval;
}

bool TiledRenderer::loading() const {
    return this->queue.pending() || (this->builder && this->builder->finished()) || this->refine_due();
}

void TiledRenderer::draw(const ViewTransform& view) {
    auto draw_frame = [&](const Frame& frame) {
        this->image_pass.draw(frame.texture, view, false, Vec2f{frame.plan.image_min_x(), frame.plan.image_min_y()},
                              Vec2f{frame.plan.image_max_x(), frame.plan.image_max_y()}, true);
    };
    if (this->current)
        draw_frame(*this->current);
    if (this->pending)
        draw_frame(*this->pending);
}

Vec2f TiledRenderer::image_size() const {
    const ImageInfo& info = this->source->info();
    return Vec2f{static_cast<float>(info.width), static_cast<float>(info.height)};
}

} // namespace enim
