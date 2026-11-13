#include "logging.hpp"

#include <settings/settings.hpp>

#include <window/window.hpp>

#include <filesystem>

#ifdef ENIM_OS_WIN
#include <cstdio>
#include <io.h>

namespace {

/// @brief Release builds are GUI programs, so opening an image from Explorer doesn't open a console. When started
/// from a terminal, print to that terminal: attach to the parent's console and reopen the streams that aren't
/// already redirected (e.g. `enim image.bmp 2> log.txt` keeps its file).
void attach_parent_console() {
    auto redirected = [](DWORD id) {
        HANDLE handle = GetStdHandle(id);
        return handle != nullptr && handle != INVALID_HANDLE_VALUE;
    };
    bool out_redirected = redirected(STD_OUTPUT_HANDLE);
    bool err_redirected = redirected(STD_ERROR_HANDLE);
    if ((out_redirected && err_redirected) || !AttachConsole(ATTACH_PARENT_PROCESS))
        return;

    if (!out_redirected && std::freopen("CONOUT$", "w", stdout))
        SetStdHandle(STD_OUTPUT_HANDLE, reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(stdout))));
    if (!err_redirected && std::freopen("CONOUT$", "w", stderr))
        SetStdHandle(STD_ERROR_HANDLE, reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(stderr))));
}

} // namespace
#endif

int main(int argc, char* argv[]) {
#ifdef ENIM_OS_WIN
    attach_parent_console();
#endif
    enim::Settings settings = enim::parse_args(argc, argv);

    enim::setup_logger(settings);

    enim::Window window(settings);

    auto result = window.setup();
    if (!result) {
        const auto& error = result.error();
        PLOGE << error.message;
        return static_cast<int>(error.code);
    }

    auto img_load_result = window.load_image();
    if (!img_load_result)
        PLOGE << img_load_result.error().message;
    window.run();

    // errors logged by any thread, including the loading ones: the log tells which
    if (settings.exit_after_load && enim::error_counter().count() > 0)
        return static_cast<int>(enim::ErrorCode::ErrorLogged);
    return 0;
}