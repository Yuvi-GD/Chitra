#include "chitra_font.h"
#include "chitra_table.h"
#include "chitra_rhi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

/* Fontstash Configuration */
#define FONTSTASH_IMPLEMENTATION
#include "fontstash.h"

#ifndef CHITRA_FONT_ATLAS_SIZE
#define CHITRA_FONT_ATLAS_SIZE 1024
#endif

/* The main Fontstash context */
static FONScontext* s_fons = NULL;
static uint16_t     s_atlas_texture_id = 0;
static int          s_atlas_width = 0;
static int          s_atlas_height = 0;

/* =========================================================================
 * Fontstash Custom Render Callbacks
 * ========================================================================= */

static int fons_render_create(void* user_ptr, int width, int height) {
    s_atlas_width = width;
    s_atlas_height = height;
    
    /* Create an empty dynamic texture for the atlas */
    s_atlas_texture_id = chitra_rhi_get_api()->create_texture(width, height);
    return s_atlas_texture_id != 0 ? 1 : 0;
}

static int fons_render_resize(void* user_ptr, int width, int height) {
    /* Destroy old texture and create a new one */
    if (s_atlas_texture_id != 0) {
        chitra_rhi_get_api()->destroy_texture(s_atlas_texture_id);
    }
    return fons_render_create(user_ptr, width, height);
}

static void fons_render_update(void* user_ptr, int* rect, const unsigned char* data) {
    /* Handled via deferred single-upload in font_prepare() */
}

static void fons_render_draw(void* user_ptr, const float* verts, const float* tcoords, const unsigned int* colors, int nverts) {
    /* NO-OP: Quads are written directly into central text staging array */
}

static void fons_render_delete(void* user_ptr) {
    if (s_atlas_texture_id != 0) {
        chitra_rhi_get_api()->destroy_texture(s_atlas_texture_id);
        s_atlas_texture_id = 0;
    }
}

/* =========================================================================
 * Subsystem Lifecycle
 * ========================================================================= */

static void font_init(void) {
    FONSparams params = {0};
    params.width = CHITRA_FONT_ATLAS_SIZE;
    params.height = CHITRA_FONT_ATLAS_SIZE;
    params.flags = FONS_ZERO_TOPLEFT;
    params.renderCreate = fons_render_create;
    params.renderResize = fons_render_resize;
    params.renderUpdate = fons_render_update;
    params.renderDraw = fons_render_draw;
    params.renderDelete = fons_render_delete;

    s_fons = fonsCreateInternal(&params);
    if (!s_fons) {
        fprintf(stderr, "[Chitra:Font] Failed to initialize Fontstash.\n");
    }
}

static void font_term(void) {
    if (s_fons) {
        fonsDeleteInternal(s_fons);
        s_fons = NULL;
    }
}

static int font_load_font(const char* name, const char* path) {
    if (!s_fons) return -1;
    return fonsAddFont(s_fons, name, path);
}

static void font_unload_font(int font_id) {
    /* Fontstash frees all font data internally during fonsDeleteInternal() */
    (void)font_id;
}

static void font_get_metrics(int font_id, float size, float* ascender, float* descender, float* line_height) {
    if (!s_fons || font_id == FONS_INVALID) {
        if (ascender)    *ascender = 0.0f;
        if (descender)   *descender = 0.0f;
        if (line_height) *line_height = 0.0f;
        return;
    }

    fonsSetFont(s_fons, font_id);
    fonsSetSize(s_fons, size);
    fonsVertMetrics(s_fons, ascender, descender, line_height);
}

/* =========================================================================
 * Pure CPU Text Measurement (Zero Allocations)
 * ========================================================================= */

static void font_measure_text(int font_id, float size, const char* text, float* out_width, float* out_height) {
    if (!s_fons || font_id == FONS_INVALID || !text) {
        if (out_width)  *out_width = 0.0f;
        if (out_height) *out_height = 0.0f;
        return;
    }

    fonsSetFont(s_fons, font_id);
    fonsSetSize(s_fons, size);
    fonsSetAlign(s_fons, FONS_ALIGN_LEFT | FONS_ALIGN_TOP);

    float bounds[4];
    float adv = fonsTextBounds(s_fons, 0.0f, 0.0f, text, NULL, bounds);

    float ascender, descender, lineh;
    fonsVertMetrics(s_fons, &ascender, &descender, &lineh);

    if (out_width)  *out_width = adv > 0.0f ? adv : (bounds[2] - bounds[0]);
    if (out_height) *out_height = lineh;
}

static void font_measure_text_wrapped(int font_id, float size, const char* text, float max_width, float* out_width, float* out_height) {
    if (!s_fons || font_id == FONS_INVALID || !text) {
        if (out_width)  *out_width = 0.0f;
        if (out_height) *out_height = 0.0f;
        return;
    }

    fonsSetFont(s_fons, font_id);
    fonsSetSize(s_fons, size);
    fonsSetAlign(s_fons, FONS_ALIGN_LEFT | FONS_ALIGN_TOP);

    float ascender, descender, lineh;
    fonsVertMetrics(s_fons, &ascender, &descender, &lineh);

    float cx = 0.0f;
    float cy = 0.0f;
    float max_x = 0.0f;

    const char* p = text;
    const char* word_start = text;

    while (*p) {
        while (*p && *p != ' ' && *p != '\n') p++;

        bool is_newline = (*p == '\n');
        int word_len = (int)(p - word_start);

        if (word_len > 0) {
            char word_buf[512];
            int copy_len = word_len < 511 ? word_len : 511;
            memcpy(word_buf, word_start, copy_len);
            word_buf[copy_len] = '\0';

            float bounds[4];
            float adv = fonsTextBounds(s_fons, cx, cy, word_buf, NULL, bounds);

            if (max_width > 0.0f && cx > 0.0f && (cx + adv) > max_width) {
                cx = 0.0f;
                cy += lineh;
                adv = fonsTextBounds(s_fons, cx, cy, word_buf, NULL, bounds);
            }

            cx += adv;
            if (cx > max_x) max_x = cx;
        }

        if (is_newline) {
            cx = 0.0f;
            cy += lineh;
            p++;
        } else if (*p == ' ') {
            float bounds[4];
            float space_adv = fonsTextBounds(s_fons, cx, cy, " ", NULL, bounds);
            cx += space_adv;
            if (cx > max_x) max_x = cx;
            p++;
        }
        word_start = p;
    }

    if (out_width)  *out_width = max_x;
    if (out_height) *out_height = cy + lineh;
}

/* =========================================================================
 * Single-Pass Text Quad Generation into Central Staging Buffer
 * ========================================================================= */

static bool emit_text_quads(uint32_t handle, int font_id, float size, const char* text, float max_width, float x, float y) {
    if (!s_fons || font_id == FONS_INVALID || !text || !chitra_is_alive(handle)) {
        return false;
    }

    int str_len = (int)strlen(text);
    if (str_len == 0) {
        chitra_table_alloc_vertices(CHITRA_PIPELINE_TEXT, handle, 0);
        return true;
    }

    /* Reserve upper bound of vertices (at most 4 vertices per char) */
    uint32_t max_vertices = (uint32_t)str_len * 4;
    Chitra_TextVertex* v = (Chitra_TextVertex*)chitra_table_alloc_vertices(CHITRA_PIPELINE_TEXT, handle, max_vertices);
    if (!v) return false;

    /* Associate Font Atlas Texture ID with element handle */
    chitra_set_texture(handle, s_atlas_texture_id);

    fonsSetFont(s_fons, font_id);
    fonsSetSize(s_fons, size);
    fonsSetAlign(s_fons, FONS_ALIGN_LEFT | FONS_ALIGN_TOP);

    float ascender, descender, lineh;
    fonsVertMetrics(s_fons, &ascender, &descender, &lineh);

    float cx = x;
    float cy = y;
    int quad_idx = 0;
    const uint32_t default_white = 0xFFFFFFFF;

    const char* p = text;
    const char* word_start = text;

    while (*p && quad_idx < str_len) {
        while (*p && *p != ' ' && *p != '\n') p++;

        bool is_newline = (*p == '\n');
        int word_len = (int)(p - word_start);

        if (word_len > 0) {
            char word_buf[512];
            int copy_len = word_len < 511 ? word_len : 511;
            memcpy(word_buf, word_start, copy_len);
            word_buf[copy_len] = '\0';

            float bounds[4];
            float adv = fonsTextBounds(s_fons, cx, cy, word_buf, NULL, bounds);

            if (max_width > 0.0f && cx > x && ((cx + adv) - x) > max_width) {
                cx = x;
                cy += lineh;
            }

            FONStextIter iter;
            fonsTextIterInit(s_fons, &iter, cx, cy, word_buf, NULL);
            FONSquad q;

            while (fonsTextIterNext(s_fons, &iter, &q) && quad_idx < str_len) {
                int base = quad_idx * 4;

                /* Top-Left */
                v[base + 0].pos[0] = q.x0; v[base + 0].pos[1] = q.y0;
                v[base + 0].uv[0]  = q.s0; v[base + 0].uv[1]  = q.t0;
                v[base + 0].color  = default_white;

                /* Top-Right */
                v[base + 1].pos[0] = q.x1; v[base + 1].pos[1] = q.y0;
                v[base + 1].uv[0]  = q.s1; v[base + 1].uv[1]  = q.t0;
                v[base + 1].color  = default_white;

                /* Bottom-Right */
                v[base + 2].pos[0] = q.x1; v[base + 2].pos[1] = q.y1;
                v[base + 2].uv[0]  = q.s1; v[base + 2].uv[1]  = q.t1;
                v[base + 2].color  = default_white;

                /* Bottom-Left */
                v[base + 3].pos[0] = q.x0; v[base + 3].pos[1] = q.y1;
                v[base + 3].uv[0]  = q.s0; v[base + 3].uv[1]  = q.t1;
                v[base + 3].color  = default_white;

                quad_idx++;
            }
            cx += adv;
        }

        if (is_newline) {
            cx = x;
            cy += lineh;
            p++;
        } else if (*p == ' ') {
            float bounds[4];
            float space_adv = fonsTextBounds(s_fons, cx, cy, " ", NULL, bounds);
            cx += space_adv;
            p++;
        }
        word_start = p;
    }

    /* In-place shrink to exact emitted vertex count (zeros dead hole overhead) */
    uint32_t actual_vertices = (uint32_t)quad_idx * 4;
    chitra_table_alloc_vertices(CHITRA_PIPELINE_TEXT, handle, actual_vertices);

    chitra_table_mark_dirty();
    return true;
}

static bool font_set_text(uint32_t handle, int font_id, float size, const char* text, float x, float y) {
    return emit_text_quads(handle, font_id, size, text, 0.0f, x, y);
}

static bool font_set_text_wrapped(uint32_t handle, int font_id, float size, const char* text, float max_width, float x, float y) {
    return emit_text_quads(handle, font_id, size, text, max_width, x, y);
}

/* =========================================================================
 * Direct In-Place Vertex Operations (Pure RAM, Zero String Parsing)
 * ========================================================================= */

static bool font_set_color(uint32_t handle, uint32_t color) {
    uint8_t pipe;
    uint32_t count;
    Chitra_TextVertex* v = (Chitra_TextVertex*)chitra_table_get_vertices(handle, &pipe, &count);
    if (!v || pipe != CHITRA_PIPELINE_TEXT || count == 0) return false;

    for (uint32_t i = 0; i < count; i++) {
        v[i].color = color;
    }

    chitra_table_mark_dirty();
    return true;
}

static bool font_set_pos(uint32_t handle, float x, float y) {
    uint8_t pipe;
    uint32_t count;
    Chitra_TextVertex* v = (Chitra_TextVertex*)chitra_table_get_vertices(handle, &pipe, &count);
    if (!v || pipe != CHITRA_PIPELINE_TEXT || count == 0) return false;

    float dx = x - v[0].pos[0];
    float dy = y - v[0].pos[1];

    for (uint32_t i = 0; i < count; i++) {
        v[i].pos[0] += dx;
        v[i].pos[1] += dy;
    }

    chitra_table_mark_dirty();
    return true;
}

static bool font_set_letter_spacing(uint32_t handle, float spacing) {
    uint8_t pipe;
    uint32_t count;
    Chitra_TextVertex* v = (Chitra_TextVertex*)chitra_table_get_vertices(handle, &pipe, &count);
    if (!v || pipe != CHITRA_PIPELINE_TEXT || count == 0) return false;

    uint32_t num_quads = count / 4;
    for (uint32_t k = 0; k < num_quads; k++) {
        float offset = (float)k * spacing;
        v[k * 4 + 0].pos[0] += offset;
        v[k * 4 + 1].pos[0] += offset;
        v[k * 4 + 2].pos[0] += offset;
        v[k * 4 + 3].pos[0] += offset;
    }

    chitra_table_mark_dirty();
    return true;
}

static bool font_get_bounds(uint32_t handle, float* out_width, float* out_height) {
    uint8_t pipe;
    uint32_t count;
    Chitra_TextVertex* v = (Chitra_TextVertex*)chitra_table_get_vertices(handle, &pipe, &count);
    if (!v || pipe != CHITRA_PIPELINE_TEXT || count == 0) {
        if (out_width)  *out_width = 0.0f;
        if (out_height) *out_height = 0.0f;
        return false;
    }

    float min_x = v[0].pos[0], max_x = v[0].pos[0];
    float min_y = v[0].pos[1], max_y = v[0].pos[1];

    for (uint32_t i = 1; i < count; i++) {
        if (v[i].pos[0] < min_x) min_x = v[i].pos[0];
        if (v[i].pos[0] > max_x) max_x = v[i].pos[0];
        if (v[i].pos[1] < min_y) min_y = v[i].pos[1];
        if (v[i].pos[1] > max_y) max_y = v[i].pos[1];
    }

    if (out_width)  *out_width = max_x - min_x;
    if (out_height) *out_height = max_y - min_y;
    return true;
}

/* =========================================================================
 * Single-Upload Atlas Synchronization & Accessors
 * ========================================================================= */

static void font_prepare(void) {
    if (!s_fons) return;

    int dirty[4];
    if (fonsValidateTexture(s_fons, dirty)) {
        if (s_atlas_texture_id != 0) {
            int width, height;
            const unsigned char* data = fonsGetTextureData(s_fons, &width, &height);
            chitra_rhi_get_api()->update_texture(s_atlas_texture_id, data, width, height);
        }
    }
}

static uint16_t font_get_atlas_texture_id(void) {
    return s_atlas_texture_id;
}

/* =========================================================================
 * V-Table Export
 * ========================================================================= */

static const Chitra_Font_API s_api = {
    .init                  = font_init,
    .term                  = font_term,
    .load_font             = font_load_font,
    .unload_font           = font_unload_font,
    .measure_text          = font_measure_text,
    .measure_text_wrapped  = font_measure_text_wrapped,
    .get_font_metrics      = font_get_metrics,
    .set_text              = font_set_text,
    .set_text_wrapped      = font_set_text_wrapped,
    .set_color             = font_set_color,
    .set_pos               = font_set_pos,
    .set_letter_spacing    = font_set_letter_spacing,
    .get_bounds            = font_get_bounds,
    .prepare               = font_prepare,
    .get_atlas_texture_id  = font_get_atlas_texture_id
};

const Chitra_Font_API* chitra_font_get_api(void) {
    return &s_api;
}
