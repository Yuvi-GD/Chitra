
#include "sokol_app.h"
#include "sokol_log.h"
#include "chitra_hal.h"
#include "chitra_rhi.h"
#include "chitra_cmd.h"
#include <stdio.h>

#ifdef _WIN32
#include <windows.h>
#include <dwmapi.h>
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#else
#include <time.h>
#endif

static sapp_desc s_desc = {0};
static Chitra_Config s_config = {0};

static void sokol_init(void) {
    chitra_rhi_get_api()->init();
    
#ifdef _WIN32
    HWND hwnd = (HWND)sapp_win32_get_hwnd();
    if (hwnd) {
        BOOL dark = TRUE;
        DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    }
#endif

    if (s_config.on_init) s_config.on_init();
}

static int s_last_w = 0;
static int s_last_h = 0;
static int s_dirty_frame_countdown = 3;

static void sokol_frame(void) {
    int w = sapp_width();
    int h = sapp_height();
    if (w == 0 || h == 0) return;
    
    if (w != s_last_w || h != s_last_h) {
        chitra_cmd_set_dirty();
        s_last_w = w;
        s_last_h = h;
    }
    
    if (s_config.on_frame) s_config.on_frame();
    
    if (chitra_cmd_is_dirty()) {
        /* Upload buffers OUTSIDE of the render pass if dirty */
        chitra_cmd_prepare();
        s_dirty_frame_countdown = 3; /* Draw for 3 frames to update triple-buffering */
    }
    
    if (s_dirty_frame_countdown > 0) {
        chitra_rhi_get_api()->begin_frame(w, h);
        chitra_cmd_draw();
        chitra_rhi_get_api()->end_frame();
        
        s_dirty_frame_countdown--;
    } else {
#ifdef _WIN32
        Sleep(16);
#else
        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = 16000000;
        nanosleep(&ts, NULL);
#endif
    }
}

static void sokol_cleanup(void) {
    chitra_rhi_get_api()->cleanup();
}

static void sokol_event(const sapp_event* e) {
    if (e->type == SAPP_EVENTTYPE_RESIZED) {
        chitra_cmd_set_dirty();
    }
    if (s_config.on_event) s_config.on_event(e);
}

static void hal_init(const Chitra_Config* config) {
    if (config) {
        s_config = *config;
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
