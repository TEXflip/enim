#include "tiled_renderer.hpp"

#include <algorithm>

#include <plog/Log.h>

#include <core/cache_dir.hpp>

namespace {

// tiles waiting for the main thread, and uploaded per update()
constexpr std::size_t kTileQueueBytes = std::size_t{64} << 20;
constexpr std::size_t kTileUploadBytesPerStep = std::size_t{64} << 20;

// an incomplete tile is resampled at most this often while the pyramid is being built
constexpr auto kRefineInterval = std::chrono::milliseconds(100);
// preview passes: 1, 2, 4, 8, 16 level-0 samples per texel
constexpr int kMaxPreviewPass = 4;

} // namespace

namespace enim {

TiledRenderer::TiledRenderer(std::unique_ptr<ImageSource> source, const LoadPlan& plan, std::filesystem::path image_path,
                             std::filesystem::path cache_dir, std::size_t max_texture_size, std::function<void()> wakeup)
    : source(std::move(source)), plan(plan), image_path(std::move(image_path)), cache_dir(std::move(cache_dir)),
      max_texture_size(max_texture_size), wakeup(std::move(wakeup)), queue(kTileQueueBytes) {}

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
            PLOGI << "Removed the incomplete pyramid " << path.string();
    }
}

std::expected<void, enim::Error> TiledRenderer::init() {
    auto pass_result = this->image_pass.init();
    if (!pass_result)
        return pass_result;

    const ImageInfo& info = this->source->info();
    std::string key = cache_key(this->image_path, this->source->header_bytes());
    std::filesystem::path path = this->cache_dir / (key + ".pyr");
    auto store_result = PyramidStore::open_or_create(path, info);
    if (!store_result)
        return std::unexpected(store_result.error());
    this->store = std::move(store_result.value());

    const PyramidGeometry& geom = this->store->geometry();
    PLOGI << "Image size (WxH): " << info.width << "x" << info.height << ", " << static_cast<int>(info.layout.channels)
              << " stored / " << static_cast<int>(this->plan.texture.channels) << " frame channels";
    PLOGI << "Pyramid " << path.string() << ": " << geom.level_count() << " levels, " << (geom.file_size >> 20) << " MiB, "
              << (this->store->complete() ? "cached" : "building");

    this->upload_params = texture_upload_params(this->plan.texture);
    this->zero_tile.assign(kTileSize * kTileSize * this->plan.texture.bytes_per_pixel(), 0);

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

void TiledRenderer::start_ring(int level, SlotGrid slots) {
    const Vec2i size{static_cast<std::int32_t>(slots.x * kTileSize), static_cast<std::int32_t>(slots.y * kTileSize)};

    // keep the ring that shows something as the stale one; the other goes (its texture may be reused)
    std::optional<Ring> discarded;
    if (this->current) {
        bool shows = std::ranges::any_of(this->current->slot_state, [](const Slot& s) { return s.uploaded_pass >= 0; });
        if (shows || !this->previous) {
            discarded = std::move(this->previous);
            this->previous = std::move(this->current);
        } else {
            discarded = std::move(this->current);
        }
    }

    Ring ring;
    ring.level = level;
    ring.slots = slots;
    ring.slot_state.assign(slots.count(), Slot{});
    if (discarded && discarded->texture.size.x == size.x && discarded->texture.size.y == size.y) {
        ring.texture = std::move(discarded->texture);
    } else {
        ring.texture = Texture::create(this->plan.texture, size);
        ring.texture.allocate();
        // tile (tx, ty) is in slot (tx mod slots.x, ty mod slots.y): the window wraps around the texture
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    }
    ring.texture.clear_transparent();
    this->current = std::move(ring);
}

void TiledRenderer::assign_window(Ring& ring, const TileRect& window) {
    ring.window = window;
    window.for_each([&](std::size_t tx, std::size_t ty) {
        Slot& slot = ring.slot(tx, ty);
        if (slot.tx == tx && slot.ty == ty)
            return;
        if (slot.uploaded_pass >= 0) // never show another tile's texels
            this->upload_to_slot(ring, tx, ty, kTileSize, kTileSize, this->zero_tile.data());
        slot = Slot{.tx = tx, .ty = ty};
    });
}

void TiledRenderer::upload_to_slot(const Ring& ring, std::size_t tx, std::size_t ty, std::size_t width, std::size_t height,
                                   const std::uint8_t* pixels) const {
    glBindTexture(GL_TEXTURE_2D, ring.texture.id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, this->upload_params.alignment);
    glTexSubImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(ring.slot_x(tx) * kTileSize), static_cast<GLint>(ring.slot_y(ty) * kTileSize),
                    static_cast<GLsizei>(width), static_cast<GLsizei>(height), this->upload_params.format, this->upload_params.type,
                    pixels);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
}

bool TiledRenderer::refine_due(const Slot& slot, std::chrono::steady_clock::time_point now) const {
    if (slot.uploaded_pass < 0 || slot.complete || slot.requested_pass > slot.uploaded_pass)
        return false;
    if (this->store->progress() == slot.progress)
        return slot.uploaded_pass < kMaxPreviewPass;
    // the last rows (and the end of the build) are never deferred: nothing would wake the main thread for them
    return this->store->complete() || now - this->last_refine >= kRefineInterval;
}

void TiledRenderer::request_tiles(Ring& ring, const ViewTransform& view) {
    const auto now = std::chrono::steady_clock::now();
    TileRequest request;
    request.level = ring.level;
    bool new_work = false; // tiles only being re-sent don't need a new request
    bool refining = false;

    ring.window.for_each([&](std::size_t tx, std::size_t ty) {
        const Slot& slot = ring.slot(tx, ty);
        if (slot.requested_pass < 0) {
            request.tiles.push_back(TileJob{tx, ty, 0});
            new_work = true;
        } else if (slot.requested_pass > slot.uploaded_pass) {
            // in flight: a new request cancels it, so it goes again
            request.tiles.push_back(TileJob{tx, ty, slot.requested_pass});
        } else if (this->refine_due(slot, now)) {
            request.tiles.push_back(TileJob{tx, ty, std::min(slot.uploaded_pass + 1, kMaxPreviewPass)});
            new_work = refining = true;
        }
    });
    if (!new_work)
        return;

    // nearest to the view's center first
    const LevelRect visible = visible_rect(view, this->store->geometry(), ring.level);
    const double cx = (static_cast<double>(visible.x0) + visible.x1) * 0.5;
    const double cy = (static_cast<double>(visible.y0) + visible.y1) * 0.5;
    auto distance = [&](const TileJob& job) {
        double dx = (job.tx + 0.5) * kTileSize - cx, dy = (job.ty + 0.5) * kTileSize - cy;
        return dx * dx + dy * dy;
    };
    std::ranges::sort(request.tiles, [&](const TileJob& a, const TileJob& b) { return distance(a) < distance(b); });

    const std::uint64_t progress = this->store->progress();
    for (const TileJob& job : request.tiles) {
        Slot& slot = ring.slot(job.tx, job.ty);
        slot.requested_pass = job.pass;
        slot.progress = progress;
    }
    if (refining)
        this->last_refine = now;
    request.generation = this->next_generation++;
    // PLOGD << "Tile request " << request.generation << ": level " << ring.level << ", " << request.tiles.size()
    //            << " tiles of window " << ring.window.width() << "x" << ring.window.height();
    this->sampler->post(std::move(request));
}

void TiledRenderer::upload_tiles() {
    std::size_t uploaded_bytes = 0;
    while (uploaded_bytes < kTileUploadBytesPerStep) {
        std::optional<UploadTile> tile = this->queue.pop();
        if (!tile)
            break;
        if (!this->current || tile->level != this->current->level)
            continue; // a level not shown anymore
        const TileJob& job = tile->job;
        Slot& slot = this->current->slot(job.tx, job.ty);
        if (slot.tx != job.tx || slot.ty != job.ty)
            continue; // the slot holds another tile now
        if (!tile->complete && job.pass < slot.uploaded_pass)
            continue; // older than what the slot shows
        this->upload_to_slot(*this->current, job.tx, job.ty, tile->width, tile->height, tile->pixels.data());
        uploaded_bytes += tile->pixels.size();
        slot.uploaded_pass = job.pass;
        slot.requested_pass = std::max(slot.requested_pass, job.pass);
        slot.complete = tile->complete;
    }
    if (uploaded_bytes > 0)
        if (GLenum err = glGetError(); err != GL_NO_ERROR)
            PLOGE << "GL error " << err << " while uploading tiles";
}

void TiledRenderer::check_build() {
    if (!this->builder || !this->builder->finished())
        return;
    this->build_thread.join();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - this->build_start);
    const auto& result = this->builder->result();
    if (result && *result)
        PLOGI << "Pyramid built in " << elapsed.count() << " ms";
    else if (result)
        PLOGE << "Pyramid build failed after " << elapsed.count() << " ms: " << result->error().message;
    this->builder.reset();
}

bool TiledRenderer::shows_view(const Ring& ring, const ViewTransform& view) const {
    // the visible tiles the ring can hold (all of them, unless capped by the texture size)
    TileRect visible = tiles_of(visible_rect(view, this->store->geometry(), ring.level)).intersect(ring.window);
    return visible.all_of([&](std::size_t tx, std::size_t ty) { return ring.slot(tx, ty).uploaded_pass >= 0; });
}

void TiledRenderer::update(const ViewTransform& view) {
    this->upload_tiles();
    this->check_build();

    const PyramidGeometry& geom = this->store->geometry();
    const int level = choose_level(view.zoom, geom.level_count());
    const SlotGrid slots = ring_slots(view.window_size, geom, level, this->max_texture_size);
    if (!this->current || this->current->level != level || this->current->slots != slots)
        this->start_ring(level, slots);

    this->assign_window(*this->current, tile_window(view, geom, level, slots));
    this->request_tiles(*this->current, view);

    if (this->previous && this->shows_view(*this->current, view))
        this->previous.reset();
}

bool TiledRenderer::loading() const {
    if (this->queue.pending() || (this->builder && this->builder->finished()))
        return true;
    if (!this->current)
        return false;
    const auto now = std::chrono::steady_clock::now();
    const Ring& ring = *this->current;
    return ring.window.any_of([&](std::size_t tx, std::size_t ty) { return this->refine_due(ring.slot(tx, ty), now); });
}

bool TiledRenderer::load_finished() const {
    if (this->builder || !this->current || this->loading())
        return false;
    const Ring& ring = *this->current;
    return ring.window.all_of([&](std::size_t tx, std::size_t ty) {
        const Slot& slot = ring.slot(tx, ty);
        return slot.uploaded_pass >= 0 && slot.requested_pass <= slot.uploaded_pass;
    });
}

void TiledRenderer::draw_ring(const Ring& ring, const ViewTransform& view) const {
    if (ring.window.empty())
        return;
    const PyramidLevel& l = this->store->geometry().levels[ring.level];
    Vec2f window_min{static_cast<float>(ring.window.tx0 * kTileSize), static_cast<float>(ring.window.ty0 * kTileSize)};
    Vec2f window_max{static_cast<float>(std::min(ring.window.tx1 * kTileSize, l.width)),
                     static_cast<float>(std::min(ring.window.ty1 * kTileSize, l.height))};
    this->image_pass.draw_ring(ring.texture, view, ring.level, window_min, window_max);
}

void TiledRenderer::draw(const ViewTransform& view) {
    if (this->previous)
        this->draw_ring(*this->previous, view);
    if (this->current)
        this->draw_ring(*this->current, view);
}

Vec2f TiledRenderer::image_size() const {
    const ImageInfo& info = this->source->info();
    return Vec2f{static_cast<float>(info.width), static_cast<float>(info.height)};
}

} // namespace enim
