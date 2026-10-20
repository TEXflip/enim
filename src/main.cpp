#include "dispatcher.hpp"
#include "image_view.hpp"
#include "logging.hpp"

#include <settings/settings.hpp>

#include <window/window.hpp>

#include <filesystem>

int main(int argc, char* argv[]) {
    enim::Settings settings = enim::parse_args(argc, argv);

    enim::setup_logger(settings);

    enim::Window window(settings);

    auto result = window.setup();
    if (!result) {
        const auto& error = result.error();
        PLOG_ERROR << error.message;
        std::exit(static_cast<int>(error.code));
    }
    
    auto img_load_result = window.load_image();
    if (!img_load_result)
        PLOG_ERROR << img_load_result.error().message;
    window.run();

    return 0;
}