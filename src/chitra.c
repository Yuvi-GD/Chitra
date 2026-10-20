#include "chitra.h"
#include "chitra_table.h"
#include "chitra_font.h"
#include "chitra_batch.h"
#include "chitra_clip.h"
#include "chitra_texture.h"
#include "chitra_rhi.h"
#include "chitra_hal.h"
#include <stdio.h>

static int s_res_x = 0;
static int s_res_y = 0;

/* =========================================================================
 * Internal Synchronization Hooks (Invoked by Frame Loop)
 * ========================================================================= */

static void chitra_internal_init(void) {
    chitra_table_init();
    chitra_batch_init();
    chitra_clip_init();
    chitra_texture_init();
    chitra_font_api()->init();
}

static void chitra_internal_term(void) {
    chitra_font_api()->term();
    chitra_texture_term();
    chitra_clip_term();
    chitra_batch_term();
    chitra_table_term();
}

static void chitra_internal_prepare(void) {
    /* 1. Single-Upload Font Atlas & Dynamic Texture Atlas Synchronization */
    chitra_font_api()->prepare();
    chitra_texture_prepare();

    /* 2. Process sorting, 32-bit index streaming, and batch coalescing (only when scene changes) */
    if (chitra_table_is_dirty()) {
        chitra_batch_prepare();
        chitra_table_clear_dirty();
    }

    /* 3. Stream active geometry to transient GPU buffers for rendered frame */
    uint32_t idx_count = 0;
    const uint32_t* indices = chitra_batch_get_indices(&idx_count);
    if (indices && idx_count > 0) {
        chitra_rhi_api()->upload_indices(indices, idx_count * sizeof(uint32_t));
    }

    /* Upload per-pipeline staging vertex buffers from central table */
    for (uint8_t p = 0; p < CHITRA_PIPELINE_COUNT; p++) {
        const Chitra_PipelineData* pipe = chitra_table_get_pipeline(p);
        if (pipe && pipe->cursor > 0) {
            chitra_rhi_api()->upload_vertices(p, pipe->data, pipe->cursor * pipe->stride);
        }
    }
}

/* =========================================================================
 * Standalone Engine Execution Loop
 * ========================================================================= */

void chitra_run(const Chitra_Config* config) {
    if (!config && !chitra_hal_api() && !chitra_rhi_api()) return;

    /* Run the HAL Application Loop */
    chitra_hal_api()->init(config);
    chitra_hal_api()->run();
}

void chitra_quit(void) {
    chitra_hal_api()->quit();
}

/* =========================================================================
 * Embedded Engine Execution Loop
 * ========================================================================= */

void chitra_init(void) {
    chitra_rhi_api()->init();
    chitra_internal_init();
}

void chitra_begin_frame(int framebuffer_w, int framebuffer_h, float dpi_scale) {
    (void)dpi_scale;
    chitra_internal_prepare();
    chitra_rhi_api()->begin_frame(framebuffer_w, framebuffer_h);
}

void chitra_render(void) {
    if (!chitra_rhi_api()) return;
    uint32_t batch_count = 0;
    const Chitra_DrawBatch* batches = chitra_batch_get_batches(&batch_count);

    for (uint32_t i = 0; i < batch_count; i++) {
        const Chitra_DrawBatch* b = &batches[i];

        /* Apply hardware scissor clipping */
        const Chitra_ClipRect* clip = chitra_get_clip(b->clip_id);
        if (clip) {
            chitra_rhi_api()->set_scissor(clip->x, clip->y, clip->w, clip->h);
        }

        /* Dispatch draw call */
        chitra_rhi_api()->draw_batch(b);
    }
}

void chitra_end_frame(void) {
    chitra_rhi_api()->end_frame();
}

void chitra_frame(void) {
    int res_x = 0, res_y = 0;
    chitra_get_resolution(&res_x, &res_y);
    chitra_begin_frame(res_x, res_y, 1.0f);
    chitra_render();
    chitra_end_frame();
}

void chitra_cleanup(void) {
    chitra_internal_term();
    chitra_rhi_api()->cleanup();
}

void chitra_mark_dirty(void) {
    chitra_table_mark_dirty();
}

void chitra_set_resolution(int res_x, int res_y) {
    s_res_x = (res_x > 0) ? res_x : 0;
    s_res_y = (res_y > 0) ? res_y : 0;
    chitra_table_mark_dirty();
}

void chitra_get_resolution(int* out_x, int* out_y) {
    if (out_x) *out_x = (s_res_x > 0) ? s_res_x : chitra_width();
    if (out_y) *out_y = (s_res_y > 0) ? s_res_y : chitra_height();
}

void chitra_map_pointer(float in_x, float in_y, float* out_x, float* out_y) {
    int res_x = 0, res_y = 0;
    chitra_get_resolution(&res_x, &res_y);
    int w = chitra_width();
    int h = chitra_height();
    if (out_x) {
        *out_x = (w > 0 && res_x > 0) ? (in_x * (float)res_x / (float)w) : in_x;
    }
    if (out_y) {
        *out_y = (h > 0 && res_y > 0) ? (in_y * (float)res_y / (float)h) : in_y;
    }
}

/* =========================================================================
 * Texture Public Router
 * ========================================================================= */

uint16_t chitra_create_texture_rgba(int width, int height) {
    return chitra_rhi_api()->create_texture_rgba(width, height);
}

void chitra_update_texture_rgba(uint16_t id, const void* pixels, int width, int height) {
    chitra_rhi_api()->update_texture_rgba(id, pixels, width, height);
}

void chitra_destroy_texture(uint16_t id) {
    chitra_rhi_api()->destroy_texture(id);
}

/* =========================================================================
 * Typography Public Router
 * ========================================================================= */

int chitra_load_font(const char* name, const char* path) {
    return chitra_font_api()->load_font(name, path);
}

void chitra_unload_font(int font_id) {
    chitra_font_api()->unload_font(font_id);
}

void chitra_measure_text(int font_id, float size, const char* text, float* out_width, float* out_height) {
    chitra_font_api()->measure_text(font_id, size, text, out_width, out_height);
}

void chitra_measure_text_wrapped(int font_id, float size, const char* text, float max_width, float* out_width, float* out_height) {
    chitra_font_api()->measure_text_wrapped(font_id, size, text, max_width, out_width, out_height);
}

void chitra_get_font_metrics(int font_id, float size, float* ascender, float* descender, float* line_height) {
    chitra_font_api()->get_font_metrics(font_id, size, ascender, descender, line_height);
}

bool chitra_text_set(uint32_t handle, int font_id, float size, const char* text, float x, float y) {
    return chitra_font_api()->set_text(handle, font_id, size, text, x, y);
}

bool chitra_text_set_wrapped(uint32_t handle, int font_id, float size, const char* text, float max_width, float x, float y) {
    return chitra_font_api()->set_text_wrapped(handle, font_id, size, text, max_width, x, y);
}

bool chitra_text_set_color(uint32_t handle, uint32_t color) {
    return chitra_font_api()->set_color(handle, color);
}

bool chitra_text_set_pos(uint32_t handle, float x, float y) {
    return chitra_font_api()->set_pos(handle, x, y);
}

bool chitra_text_set_opacity(uint32_t handle, float opacity) {
    return chitra_font_api()->set_opacity(handle, opacity);
}

bool chitra_text_set_rotation(uint32_t handle, float origin_x, float origin_y, float angle_rad) {
    return chitra_font_api()->set_rotation(handle, origin_x, origin_y, angle_rad);
}

bool chitra_text_set_scale(uint32_t handle, float origin_x, float origin_y, float scale_x, float scale_y) {
    return chitra_font_api()->set_scale(handle, origin_x, origin_y, scale_x, scale_y);
}

bool chitra_text_set_letter_spacing(uint32_t handle, float spacing) {
    return chitra_font_api()->set_letter_spacing(handle, spacing);
}

bool chitra_text_get_bounds(uint32_t handle, float* out_width, float* out_height) {
    return chitra_font_api()->get_bounds(handle, out_width, out_height);
}
