#pragma once

#include <cstdio>
#include <cstring>
#include <expected>
#include <filesystem>
#include <fstream>
#include <functional>
#include <print>
#include <unordered_map>

#include <plog/Log.h>

#include "errors.hpp"
#include "formats.hpp"
#include "image_view.hpp"
#include "settings.hpp"

#include "bmp.hpp"

using FileHeaderFactory = std::function<std::unique_ptr<enim::FileReader>()>;
namespace enim {

static const std::unordered_map<std::string_view, FileHeaderFactory> HEADER_MAP{
    {"BMP", []() { return std::make_unique<enim::BmpReader>(); }}
};

/// @brief Detect the file format of the given image file based on its signature.
/// @param path The path to the image file.
/// @return An expected containing the detected FileFormat on success, or an Error on failure.
const std::expected<enim::FileFormat, enim::Error> detect_format(const std::filesystem::path& path) {
    enim::ErrorCode default_error = enim::ErrorCode::FileOpenError;

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::unexpected(enim::Error{default_error, "Couldn't open file"});
    }

    // Read just enough bytes to cover every signature's offset + size.
    std::array<std::uint8_t, enim::kMaxSignatureReadSize> signature_buffer{};
    file.read(reinterpret_cast<char*>(signature_buffer.data()), signature_buffer.size());

    // Number of bytes actually read (file may be shorter than signature_buffer).
    const std::streamsize bytes_read = file.gcount();
    if (bytes_read <= 0) {
        return std::unexpected(enim::Error{default_error, "Empty or unreadable file"});
    }
    const std::size_t available = static_cast<std::size_t>(bytes_read);

    for (const auto& format : enim::FORMATS) {
        const auto& sig = format.signature;

        // Skip formats whose signature would run past what we actually read.
        if (sig.offset + sig.size > available) {
            continue;
        }

        // matched
        if (std::memcmp(signature_buffer.data() + sig.offset, sig.bytes.data(), sig.size) == 0) {
            return format;
        }
    }

    return std::unexpected(enim::Error{default_error, "Unknown image format"});
}

std::expected<std::unique_ptr<enim::ImageView>, enim::Error> dispatch(const Settings& settings) {
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
    if (!preload_result) {
        return std::unexpected(preload_result.error());
    }

    if (settings.force_normal_mode) {
        return std::make_unique<enim::NormalModeImageView>(std::move(header));
    }

    if (settings.force_fast_mode) {
        return std::make_unique<enim::FastModeImageView>(std::move(header));
    }

    std::size_t size = header->get_size();

    // TODO: read how much VRAM is available and decide between fast and normal mode based on that

    return std::make_unique<enim::FastModeImageView>(std::move(header));
}

} // namespace enim