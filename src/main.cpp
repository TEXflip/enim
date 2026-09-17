#include <filesystem>

#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_glue.h"
#include "sokol_log.h"

#include "dispatcher.hpp"
#include "image_view.hpp"

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
    if (argc == 2) {
        std::filesystem::path image_path = argv[1];
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