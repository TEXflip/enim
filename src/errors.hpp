#pragma once

#include <string>

namespace enim {

enum ErrorCode {
    FileOpenError = 1,
};

struct Error {
    ErrorCode code;
    std::string message;
};

} // namespace enim