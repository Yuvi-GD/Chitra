#include "chitra_font.h"
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

/* The Sokol Image representing our Font Atlas in VRAM */
static uint16_t s_atlas_texture_id = 0;
static int s_atlas_width = 0;
static int s_atlas_height = 0;

/* =========================================================================
 * Fontstash Custom Render Backend
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
    /* rect is [x, y, x + width, y + height] */
    int x = rect[0];
    int y = rect[1];
    int w = rect[2] - rect[0];
    int h = rect[3] - rect[1];

    /* We update the GPU texture with the new atlas data */
    /* Since we don't have a partial texture update API in Chitra_RHI yet, 
       we'll just upload the whole thing for now or add update_texture to RHI. */
    if (s_atlas_texture_id != 0) {
        chitra_rhi_get_api()->update_texture(s_atlas_texture_id, data, s_atlas_width, s_atlas_height);
    }
}

static void fons_render_draw(void* user_ptr, const float* verts, const float* tcoords, const unsigned int* colors, int nverts) {
    /* NO-OP! We don't use Fontstash's immediate-mode drawing.
       Instead, we use fonsTextIter to generate vertices and push them to Chitra arenas! */
}

static void fons_render_delete(void* user_ptr) {
    if (s_atlas_texture_id != 0) {
        chitra_rhi_get_api()->destroy_texture(s_atlas_texture_id);
        s_atlas_texture_id = 0;
    }
}

/* =========================================================================
 * Chitra Font API Implementation
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

static void font_process_text(int font_id, float size, float wrap_width, float start_x, float start_y, const char* text,
                              float* out_w, float* out_h,
                              int z_index, uint32_t color, bool draw) {
    if (!s_fons || font_id == FONS_INVALID || !text) {
        if (out_w) *out_w = 0;
        if (out_h) *out_h = 0;
        return;
    }

    fonsSetFont(s_fons, font_id);
    fonsSetSize(s_fons, size);
    fonsSetAlign(s_fons, FONS_ALIGN_LEFT | FONS_ALIGN_TOP);

    float ascender, descender, lineh;
    fonsVertMetrics(s_fons, &ascender, &descender, &lineh);

    float cx = start_x;
    float cy = start_y;
    float max_x = start_x;

    const char* p = text;
    const char* word_start = text;
    
    Chitra_TextVertex* vertices = NULL;
    int quad_count = 0;
    size_t len = strlen(text);

    if (draw) {
        vertices = (Chitra_TextVertex*)malloc(sizeof(Chitra_TextVertex) * len * 4);
    }

    while (*p) {
        // Find next word boundary
        while (*p && *p != ' ' && *p != '\n') p++;
        
        bool is_newline = (*p == '\n');
        
        int word_len = p - word_start;
        if (word_len > 0) {
            char word_buf[512];
            int copy_len = word_len < 511 ? word_len : 511;
            memcpy(word_buf, word_start, copy_len);
            word_buf[copy_len] = '\0';
            
            float bounds[4];
            float adv = fonsTextBounds(s_fons, cx, cy, word_buf, NULL, bounds);
            
            if (wrap_width > 0 && cx > start_x && ((cx + adv) - start_x) > wrap_width) {
                // Wrap to next line BEFORE this word
                cx = start_x;
                cy += lineh;
                adv = fonsTextBounds(s_fons, cx, cy, word_buf, NULL, bounds);
            }
            
            if (draw && vertices) {
                FONStextIter iter;
                fonsTextIterInit(s_fons, &iter, cx, cy, word_buf, NULL);
                FONSquad q;
                
                while (fonsTextIterNext(s_fons, &iter, &q)) {
                    if (quad_count >= len) break;
                    
                    int i = quad_count * 4;
        
                    /* 0: Top-Left */
                    vertices[i+0].pos[0] = q.x0; vertices[i+0].pos[1] = q.y0;
                    vertices[i+0].uv[0]  = q.s0; vertices[i+0].uv[1]  = q.t0;
                    vertices[i+0].color  = color;
                    
                    /* 1: Top-Right */
                    vertices[i+1].pos[0] = q.x1; vertices[i+1].pos[1] = q.y0;
                    vertices[i+1].uv[0]  = q.s1; vertices[i+1].uv[1]  = q.t0;
                    vertices[i+1].color  = color;
                    
                    /* 2: Bottom-Right */
                    vertices[i+2].pos[0] = q.x1; vertices[i+2].pos[1] = q.y1;
                    vertices[i+2].uv[0]  = q.s1; vertices[i+2].uv[1]  = q.t1;
                    vertices[i+2].color  = color;
                    
                    /* 3: Bottom-Left */
                    vertices[i+3].pos[0] = q.x0; vertices[i+3].pos[1] = q.y1;
                    vertices[i+3].uv[0]  = q.s0; vertices[i+3].uv[1]  = q.t1;
                    vertices[i+3].color  = color;
        
                    quad_count++;
                }
            }
            cx += adv;
            if (cx > max_x) max_x = cx;
        }

        if (is_newline) {
            cx = start_x;
            cy += lineh;
            p++;
        } else if (*p == ' ') {
            // Measure space
            float bounds[4];
            float space_adv = fonsTextBounds(s_fons, cx, cy, " ", NULL, bounds);
            cx += space_adv;
            if (cx > max_x) max_x = cx;
            p++;
        }
        word_start = p;
    }

    if (draw && quad_count > 0 && vertices) {
        chitra_cmd_push_text(z_index, s_atlas_texture_id, vertices, quad_count);
    }
    
    if (vertices) free(vertices);

    if (out_w) *out_w = (max_x - start_x);
    if (out_h) *out_h = (cy - start_y) + lineh;
}

static void font_push_text(int z_index, int font_id, float size, float wrap_width, float x, float y, const char* text, uint32_t color) {
    font_process_text(font_id, size, wrap_width, x, y, text, NULL, NULL, z_index, color, true);
}

static void font_measure_text(int font_id, float size, float wrap_width, const char* text, float* out_width, float* out_height) {
    font_process_text(font_id, size, wrap_width, 0, 0, text, out_width, out_height, 0, 0, false);
}

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

static const Chitra_Font_API s_api = {
    .init = font_init,
    .term = font_term,
    .load_font = font_load_font,
    .push_text = font_push_text,
    .measure_text = font_measure_text,
    .prepare = font_prepare
};

const Chitra_Font_API* chitra_font_get_api(void) {
    return &s_api;
}
