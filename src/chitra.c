#include "chitra.h"
#include "chitra_hal.h"
#include "chitra_rhi.h"
#include "chitra_cmd.h"
#include <stdio.h>

static const Chitra_HAL_API* g_hal = NULL;
static const Chitra_RHI_API* g_rhi = NULL;

void chitra_run(const Chitra_Config* config) {
    if (!config) return;
    
    printf("[Chitra] Core Initializing: %dx%d '%s'\n", config->width, config->height, config->title);
    
    /* 1. Bind Backends */
    g_hal = chitra_hal_get_api();
    g_rhi = chitra_rhi_get_api();
    
    if (!g_hal || !g_rhi) {
        printf("[Chitra] Error: Failed to load backends.\n");
        return;
    }
    
    /* 2. Initialize Internal Subsystems */
    chitra_cmd_init();
    
    /* 3. Run the HAL Application Loop */
    g_hal->init(config);
    g_hal->run();
    
    /* 4. Teardown */
    chitra_cmd_term();
}

/* =========================================================================
 * Drawing API (Forwards to cmd arena)
 * ========================================================================= */

void chitra_draw_rect(int z_index, float x, float y, float w, float h,
                      float radius, float border,
                      uint32_t fill_color, uint32_t border_color) {
    chitra_cmd_push_rect(z_index, x, y, w, h, radius, border, fill_color, border_color);
}

void chitra_draw_text_vertices(int z_index, uint16_t texture_id,
                               const Chitra_TextVertex* vertices, int quad_count) {
    chitra_cmd_push_text(z_index, texture_id, vertices, quad_count);
}

void chitra_push_clip(int x, int y, int w, int h) {
    chitra_cmd_push_clip(x, y, w, h);
}

void chitra_pop_clip(void) {
    chitra_cmd_pop_clip();
}

void chitra_mark_dirty(void) {
    chitra_cmd_set_dirty();
}
