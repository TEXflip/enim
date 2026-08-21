#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_glue.h"
#include "sokol_log.h"

static void init() {
    sg_setup(&(sg_desc){
        .environment = sglue_environment(),
        .logger.func = slog_func
    });
}

static void frame() {
    sg_begin_pass(&(sg_pass){
        .swapchain = sglue_swapchain()
    });

    sg_end_pass();
    sg_commit();
}

static void cleanup() {
    sg_shutdown();
}

sapp_desc sokol_main(int argc, char* argv[]) {
    return (sapp_desc){
        .init_cb = init,
        .frame_cb = frame,
        .cleanup_cb = cleanup,
        .width = 1280,
        .height = 720,
        .window_title = "My Sokol App"
    };
}