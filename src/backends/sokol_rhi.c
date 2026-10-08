#if defined(_WIN32)
#define SOKOL_D3D11
#elif defined(__APPLE__)
#define SOKOL_METAL
#else
#define SOKOL_GLCORE
#endif

#include "sokol_gfx.h"
#include "sokol_glue.h"
#include "sokol_log.h"
#include "chitra_rhi.h"
#include <stdio.h>

static void rhi_init(void) {
    sg_desc desc = {
        .environment = sglue_environment(),
        .logger.func = slog_func,
    };
    sg_setup(&desc);
    printf("[Chitra:Sokol] RHI Initialized.\n");
}

static void rhi_begin_frame(int width, int height) {
    sg_pass_action pass_action = {
        .colors[0] = { .load_action = SG_LOADACTION_CLEAR, .clear_value = { 0.1f, 0.1f, 0.1f, 1.0f } }
    };
    sg_begin_pass(&(sg_pass){ .action = pass_action, .swapchain = sglue_swapchain() });
}

static void rhi_end_frame(void) {
    sg_end_pass();
    sg_commit();
}

static void rhi_set_scissor(int x, int y, int w, int h) {
    sg_apply_scissor_rect(x, y, w, h, true);
}

static void rhi_draw_rect(const Chitra_ShapeVertex* vertices, int count) {
    // Pipeline and drawing logic will go here
}

static void rhi_cleanup(void) {
    sg_shutdown();
    printf("[Chitra:Sokol] RHI Shutdown.\n");
}

static const Chitra_RHI_API s_api = {
    .init = rhi_init,
    .begin_frame = rhi_begin_frame,
    .end_frame = rhi_end_frame,
    .set_scissor = rhi_set_scissor,
    .draw_rect = rhi_draw_rect,
    .cleanup = rhi_cleanup
};

const Chitra_RHI_API* chitra_rhi_get_api(void) {
    return &s_api;
}
