#if defined(_WIN32)
#define SOKOL_D3D11
#elif defined(__APPLE__)
#define SOKOL_METAL
#else
#define SOKOL_GLCORE
#endif

#include "sokol_app.h"
#include "sokol_log.h"
#include "chitra_hal.h"
#include "chitra_rhi.h"
#include <stdio.h>

static sapp_desc s_desc = {0};

static void sokol_init(void) {
    printf("[Chitra:Sokol] HAL Initialized.\n");
    chitra_rhi_get_api()->init();
}

static void sokol_frame(void) {
    int w = sapp_width();
    int h = sapp_height();
    if (w == 0 || h == 0) return;
    
    chitra_rhi_get_api()->begin_frame(w, h);
    // Future: Chitra core will flush command queues here
    chitra_rhi_get_api()->end_frame();
}

static void sokol_cleanup(void) {
    chitra_rhi_get_api()->cleanup();
    printf("[Chitra:Sokol] HAL Cleanup.\n");
}

static void sokol_event(const sapp_event* e) {
    // Pass events to Chitra core here later
}

static void hal_init(const Chitra_Config* config) {
    if (config) {
        s_desc.width = config->width;
        s_desc.height = config->height;
        s_desc.window_title = config->title;
    }
    s_desc.init_cb = sokol_init;
    s_desc.frame_cb = sokol_frame;
    s_desc.cleanup_cb = sokol_cleanup;
    s_desc.event_cb = sokol_event;
    s_desc.logger.func = slog_func;
}

static void hal_run(void) {
    sapp_run(&s_desc);
}

static void hal_quit(void) {
    sapp_request_quit();
}

static const Chitra_HAL_API s_api = {
    .init = hal_init,
    .run = hal_run,
    .quit = hal_quit
};

const Chitra_HAL_API* chitra_hal_get_api(void) {
    return &s_api;
}
