#pragma once

#include <expected>
#include <string>
#include <utility>

namespace enim {

/// @brief Error categories. A setup error (window, OpenGL) is also the process exit code, and --exit-after-load exits
/// with ErrorLogged if any error was logged. Keep them below 126: shells use 126+ for "not executable" and signals,
/// and 3 is what std::abort() returns on Windows. tests/run_image.cmake reads the names from this file: keep one
/// "Name = value," per line.
enum ErrorCode {
    // generic
    ErrorLogged = 1,      // an error was logged without a code
    InvalidArguments = 2, // bad command line or environment variable

    // window and OpenGL context
    WindowCreationError = 10,
    GLLoaderError = 11,  // GL functions couldn't be loaded
    GLVersionError = 12, // OpenGL 3.3 isn't available

    // GPU resources
    ShaderCompileError = 20,
    ShaderLinkError = 21,
    FramebufferError = 22,
    GLError = 23, // glGetError reported an error

    // image file
    FileOpenError = 30,          // missing, unreadable or empty file
    UnknownFormatError = 31,     // no known signature
    UnsupportedFormatError = 32, // known format, unsupported variant (e.g. CMYK JPEG, compressed BMP)
    CorruptImageError = 33,      // invalid header or truncated data

    // loading
    ImageLoadError = 40,
    DecodeCancelled = 41,
    PyramidCacheError = 42,
};

struct Error {
    ErrorCode code;
    std::string message;
};

/// @brief An Error as std::unexpected, for functions returning std::expected<T, Error>. Inside namespace enim the code
/// needs no prefix (ErrorCode is unscoped): return make_error(CorruptImageError, "QOI data is truncated");
inline std::unexpected<Error> make_error(ErrorCode code, std::string message) {
    return std::unexpected(Error{code, std::move(message)});
}

} // namespace enim
