#include <filesystem>

#include <sokol_app.h>
#include <sokol_gfx.h>
#include <sokol_glue.h>
#include <sokol_log.h>

#include "dispatcher.hpp"
#include "image_view.hpp"
#include "argparse.hpp"
#include "logging.hpp"

static void init() {
    sg_desc desc{};
    desc.environment = sglue_environment();
    desc.logger.func = slog_func;
    sg_setup(&desc);
}

static void frame() {
    sg_pass pass{};
    pass.swapchain = sglue_swapchain();
    sg_begin_pass(&pass);

    sg_end_pass();
    sg_commit();
}

static void cleanup() {
    sg_shutdown();
}

sapp_desc sokol_main(int argc, char* argv[]) {
    ArgumentSettings settings = parse_args(argc, argv);
    
    if (settings.help) {
        std::printf("Usage: %s [options]\n", argv[0]);
        std::printf("Options:\n");
        std::printf("  --help        Show this help message\n");
        std::printf("  -d, --debug   Enable debug logging\n");
        std::exit(0);
    }

    setup_logger(settings);

    if (!settings.image_path.empty()) {
        std::filesystem::path image_path = settings.image_path;
        dispatch(image_path);
    }

    return (sapp_desc){
        .init_cb = init,
        .frame_cb = frame,
        .cleanup_cb = cleanup,
        .width = 1280,
        .height = 720,
        .window_title = "My Sokol App"
    };
}