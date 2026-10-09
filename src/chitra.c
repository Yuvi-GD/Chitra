#include "chitra.h"
#include "chitra_hal.h"
#include "chitra_rhi.h"
#include "chitra_cmd.h"
#include <stdio.h>
#include "chitra_font.h"

static const Chitra_HAL_API* g_hal = NULL;
static const Chitra_RHI_API* g_rhi = NULL;

void chitra_run(const Chitra_Config* config) {
    if (!config) return;
    
    printf("[Chitra] Core Initializing: %dx%d '%s'\n", config->width, config->height, config->title);
    
    /* Bind Backends */
    g_hal = chitra_hal_get_api();
    g_rhi = chitra_rhi_get_api();
    
    if (!g_hal || !g_rhi) {
        printf("[Chitra] Error: Failed to load backends.\n");
        return;
    }
    
    /* Run the HAL Application Loop */
    g_hal->init(config);
    g_hal->run();
}

/* =========================================================================
 * Internal Initialization (Called by HAL after RHI is ready)
 * ========================================================================= */

void chitra_internal_init(void) {
    chitra_cmd_init();
    chitra_font_get_api()->init();
}

void chitra_internal_term(void) {
    chitra_font_get_api()->term();
    chitra_cmd_term();
}

void chitra_internal_prepare(void) {
    chitra_font_get_api()->prepare();
    chitra_cmd_prepare();
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

/* =========================================================================
 * Text API (Forwards to font system)
 * ========================================================================= */

int chitra_load_font(const char* name, const char* path) {
    return chitra_font_get_api()->load_font(name, path);
}

void chitra_draw_text(int z_index, int font_id, float size, float x, float y, const char* text, uint32_t color) {
    chitra_font_get_api()->push_text(z_index, font_id, size, x, y, text, color);
}

void chitra_measure_text(int font_id, float size, const char* text, float* out_width, float* out_height) {
    chitra_font_get_api()->measure_text(font_id, size, text, out_width, out_height);
}
