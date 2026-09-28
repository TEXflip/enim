#pragma once

#include <string>

namespace enim {

enum ErrorCode {
    FileOpenError = 1,
    WindowCreationError = 2,
    ShaderCompileError = 3,
    ShaderLinkError = 4,
    ImageLoadError = 5,
};

struct Error {
    ErrorCode code;
    std::string message;
};

} // namespace enim