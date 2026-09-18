#pragma once
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <expected>
#include <print>

#include <plog/Log.h>

#include "errors.hpp"
#include "image_view.hpp"
#include "formats.hpp"

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

    return std::unexpected(enim::Error{default_error, "No matching format found"});
}

void dispatch(std::filesystem::path image_path) {
    auto format_result = detect_format(image_path);
    if (!format_result) {
        auto error = format_result.error();
        PLOG_ERROR << "Error detecting format: " << error.message;
        std::exit(static_cast<int>(error.code));
    }
    enim::FileFormat format = format_result.value();
    PLOG_INFO << "Detected format: " << format.name.data();
}