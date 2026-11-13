#include "pyramid_store.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <string>

#ifdef ENIM_OS_WIN
#include <winioctl.h>
#endif

#include <plog/Log.h>

namespace {

/// @brief True if the file at path is a complete pyramid of this geometry.
bool is_complete_match(const std::filesystem::path& path, const enim::PyramidGeometry& geom) {
    std::error_code ec;
    if (std::filesystem::file_size(path, ec) != geom.file_size || ec)
        return false;
    std::ifstream file(path, std::ios::binary);
    enim::PyramidHeader stored;
    if (!file.read(reinterpret_cast<char*>(&stored), sizeof(stored)))
        return false;
    enim::PyramidHeader expected = geom.header();
    expected.complete = 1;
    return std::memcmp(&stored, &expected, sizeof(stored)) == 0;
}

/// @brief Creates an empty file of the given size. On Windows it is sparse: writing far into a regular file first fills
/// the gap with zeros, which would stall the builder each time it starts a level further in the file.
std::expected<void, enim::Error> create_file(const std::filesystem::path& path, std::uint64_t size) {
    std::error_code ec;
    std::filesystem::remove(path, ec);
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file)
            return enim::make_error(enim::PyramidCacheError, "Pyramid cache: can't create " + path.string());
    }
#ifdef ENIM_OS_WIN
    HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle != INVALID_HANDLE_VALUE) {
        DWORD returned = 0;
        if (!DeviceIoControl(handle, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &returned, nullptr))
            PLOGW << "Pyramid cache: can't make the file sparse, writing it may be slower";
        CloseHandle(handle);
    }
#endif
    std::filesystem::resize_file(path, size, ec);
    if (ec)
        return enim::make_error(enim::PyramidCacheError, "Pyramid cache: can't allocate " + std::to_string(size >> 20) + " MiB: " + ec.message());
    return {};
}

} // namespace

namespace enim {

std::expected<std::unique_ptr<PyramidStore>, enim::Error> PyramidStore::open_or_create(const std::filesystem::path& path,
                                                                                     const ImageInfo& info) {
    auto store = std::unique_ptr<PyramidStore>(new PyramidStore());
    store->file_path = path;
    store->geom = PyramidGeometry::make(info.width, info.height, info.layout);
    store->bottom_up = info.layout.rows == RowOrder::BottomUp;
    store->rows_written = std::make_unique<std::atomic<std::size_t>[]>(store->geom.levels.size());

    bool complete = is_complete_match(path, store->geom);
    if (!complete) {
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec)
            return make_error(PyramidCacheError, "Pyramid cache: can't create " + path.parent_path().string() + ": " + ec.message());

        std::uint64_t old_size = std::filesystem::exists(path, ec) ? std::filesystem::file_size(path, ec) : 0;
        std::filesystem::space_info space = std::filesystem::space(path.parent_path(), ec);
        if (!ec && space.available + old_size < store->geom.file_size)
            return make_error(PyramidCacheError, "Pyramid cache: not enough disk space in " + path.parent_path().string() + ": " +
                                                 std::to_string(store->geom.file_size >> 20) + " MiB needed, " +
                                                 std::to_string(space.available >> 20) + " MiB available");

        if (auto created = create_file(path, store->geom.file_size); !created)
            return std::unexpected(created.error());
    }

    std::error_code ec;
    store->map.map(path.native(), ec);
    if (ec)
        return make_error(PyramidCacheError, "Pyramid cache: can't map " + path.string() + ": " + ec.message());

    if (complete) {
        store->mark_all_ready();
    } else {
        PyramidHeader header = store->geom.header(); // complete = 0 until finish()
        std::memcpy(store->base(), &header, sizeof(header));
    }
    return store;
}

void PyramidStore::mark_all_ready() {
    for (std::size_t l = 0; l < this->geom.levels.size(); ++l)
        this->rows_written[l].store(this->geom.levels[l].height, std::memory_order_relaxed);
    this->is_complete.store(true, std::memory_order_release);
}

void PyramidStore::write_row(int level, std::size_t y, const std::uint8_t* row) {
    const PyramidLevel& l = this->geom.levels[level];
    const std::size_t tile_row_bytes = kTileSize * this->geom.bpp;
    for (std::size_t tx = 0; tx < l.tiles_x; ++tx) {
        std::size_t x0 = tx * kTileSize;
        std::size_t count = std::min(kTileSize, l.width - x0);
        std::memcpy(this->base() + this->geom.pixel_offset(level, x0, y), row + x0 * this->geom.bpp,
                    std::min(tile_row_bytes, count * this->geom.bpp));
    }

    this->rows_written[level].fetch_add(1, std::memory_order_release);
    this->progress_count.fetch_add(1, std::memory_order_release);
}

std::expected<void, enim::Error> PyramidStore::finish() {
    std::error_code ec;
    this->map.sync(ec);
    if (ec)
        return make_error(PyramidCacheError, "Pyramid cache: flush failed: " + ec.message());
    PyramidHeader header = this->geom.header();
    header.complete = 1;
    std::memcpy(this->base(), &header, sizeof(header));
    this->map.sync(ec);
    if (ec)
        return make_error(PyramidCacheError, "Pyramid cache: flush failed: " + ec.message());
    this->is_complete.store(true, std::memory_order_release);
    this->progress_count.fetch_add(1, std::memory_order_release);
    return {};
}

} // namespace enim
