#include "dispatcher.hpp"

#include <render/fast_renderer.hpp>

namespace {

const std::unordered_map<std::string_view, FileHeaderFactory> HEADER_MAP{
    {"BMP", []() { return std::make_unique<enim::BmpReader>(); }}
};

} // namespace

namespace enim {

const std::expected<enim::FileFormat, enim::Error> detect_format(const std::filesystem::path& path) {
    constexpr enim::ErrorCode default_error = enim::ErrorCode::FileOpenError;

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::unexpected(enim::Error{default_error, "Couldn't open file"});
    }

    std::array<std::uint8_t, enim::kMaxSignatureReadSize> signature_buffer{};
    file.read(reinterpret_cast<char*>(signature_buffer.data()), signature_buffer.size());

    const std::streamsize bytes_read = file.gcount();
    if (bytes_read <= 0) {
        return std::unexpected(enim::Error{default_error, "Empty or unreadable file"});
    }
    const std::size_t available = static_cast<std::size_t>(bytes_read);

    for (const auto& format : enim::FORMATS) {
        const auto& sig = format.signature;

        if (sig.offset + sig.size > available) {
            continue;
        }

        if (std::memcmp(signature_buffer.data() + sig.offset, sig.bytes.data(), sig.size) == 0) {
            return format;
        }
    }

    return std::unexpected(enim::Error{default_error, "Unknown image format"});
}

std::expected<std::unique_ptr<enim::ImageRenderer>, enim::Error> dispatch(const Settings& settings, int max_texture_size) {
    auto format_result = detect_format(settings.image_path);
    if (!format_result)
        return std::unexpected(format_result.error());

    enim::FileFormat format = format_result.value();

    PLOG_INFO << "Detected format: " << format.name.data();

    auto header_factory_it = HEADER_MAP.find(format.name);

    if (header_factory_it == HEADER_MAP.end())
        return std::unexpected(enim::Error{enim::ErrorCode::FileOpenError, std::string(format.name.data()) + " Format not supported"});

    std::unique_ptr<enim::FileReader> header = header_factory_it->second();

    auto preload_result = header->load_header(settings.image_path);
    if (!preload_result)
        return std::unexpected(preload_result.error());

    Shape image_shape = header->get_shape();

    bool fast_mode = image_shape.width <= max_texture_size && image_shape.height <= max_texture_size;

    if (settings.force_fast_mode) fast_mode = true;
    if (settings.force_normal_mode) fast_mode = false;

    if (!fast_mode) {
        // TODO: return the tiled renderer once normal mode is implemented
        PLOG_WARNING << "Normal mode not implemented yet, falling back to fast mode";
    } else {
        PLOG_INFO << "Using fast mode";
    }

    return std::make_unique<enim::FastRenderer>(std::move(header));
}

} // namespace enim