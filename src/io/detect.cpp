#include "detect.hpp"

#include <cstring>
#include <fstream>
#include <functional>
#include <string>
#include <unordered_map>

#include <plog/Log.h>

#include <io/formats/bmp/bmp_source.hpp>
#include <io/formats/jpeg/jpeg_source.hpp>
#include <io/formats/qoi/qoi_source.hpp>

namespace {

using SourceFactory = std::function<std::unique_ptr<enim::ImageSource>()>;

const std::unordered_map<std::string_view, SourceFactory> SOURCE_MAP{
    {"BMP", []() { return std::make_unique<enim::BmpSource>(); }},
    {"JPEG", []() { return std::make_unique<enim::JpegSource>(); }},
    {"QOI (Quite OK Image)", []() { return std::make_unique<enim::QoiSource>(); }}
};

} // namespace

namespace enim {

std::expected<FileFormat, enim::Error> detect_format(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return make_error(FileOpenError, "Couldn't open file");
    }

    std::array<std::uint8_t, kMaxSignatureReadSize> signature_buffer{};
    file.read(reinterpret_cast<char*>(signature_buffer.data()), signature_buffer.size());

    const std::streamsize bytes_read = file.gcount();
    if (bytes_read <= 0) {
        return make_error(FileOpenError, "Empty or unreadable file");
    }
    const std::size_t available = static_cast<std::size_t>(bytes_read);

    for (const auto& format : FORMATS) {
        const auto& sig = format.signature;

        if (sig.offset + sig.size > available) {
            continue;
        }

        if (std::memcmp(signature_buffer.data() + sig.offset, sig.bytes.data(), sig.size) == 0) {
            return format;
        }
    }

    return make_error(UnknownFormatError, "Unknown image format");
}

std::expected<std::unique_ptr<ImageSource>, enim::Error> open_source(const std::filesystem::path& path) {
    auto format_result = detect_format(path);
    if (!format_result)
        return std::unexpected(format_result.error());

    const FileFormat& format = format_result.value();
    PLOG_INFO << "Detected format: " << format.name;

    auto factory_it = SOURCE_MAP.find(format.name);
    if (factory_it == SOURCE_MAP.end())
        return make_error(UnsupportedFormatError, std::string(format.name) + " Format not supported");

    std::unique_ptr<ImageSource> source = factory_it->second();

    auto open_result = source->open(path);
    if (!open_result)
        return std::unexpected(open_result.error());

    return source;
}

} // namespace enim
