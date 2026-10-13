#ifndef CHITRA_H
#define CHITRA_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Pipeline & Primitive Identifiers
 * ========================================================================= */

typedef enum {
    CHITRA_PIPELINE_SHAPE   = 0, /* SDF Rounded Boxes, Borders, Pills (40B vertex) */
    CHITRA_PIPELINE_TEXT    = 1, /* Alpha Font Atlas Quad Modulation (20B vertex) */
    CHITRA_PIPELINE_TEXTURE = 2, /* General Textured Quad Modulation (20B vertex) */
    CHITRA_PIPELINE_COUNT   = 3
} Chitra_PipelineType;

#define CHITRA_INVALID_HANDLE 0xFFFFFFFFU

/* =========================================================================
 * Vertex Layouts (Aligned to GPU Hardware Strides)
 * ========================================================================= */

typedef struct {
    float    pos[2];           /* X, Y screen coordinates in pixels */
    float    uv[2];            /* Normalized quad UV coordinates (0.0 to 1.0) */
    float    size[2];          /* Physical width and height in pixels */
    float    radius_border[2]; /* Corner radius, border thickness in pixels */
    uint32_t fill_color;       /* Packed RGBA8 (UBYTE4N) */
    uint32_t border_color;     /* Packed RGBA8 (UBYTE4N) */
} Chitra_ShapeVertex;          /* Exactly 40 bytes */

typedef struct {
    float    pos[2];           /* X, Y screen coordinates in pixels */
    float    uv[2];            /* Normalized texture/atlas UV coordinates */
    uint32_t color;            /* Packed RGBA8 tint (UBYTE4N) */
} Chitra_TextVertex;           /* Exactly 20 bytes */

/* =========================================================================
 * Scissor Clip Region
 * ========================================================================= */

typedef struct {
    int x;
    int y;
    int w;
    int h;
} Chitra_ClipRect;

/* =========================================================================
 * Unified Input & OS Event Abstraction
 * ========================================================================= */

typedef enum {
    CHITRA_EVENT_NONE = 0,
    CHITRA_EVENT_MOUSE_MOVE,
    CHITRA_EVENT_MOUSE_DOWN,
    CHITRA_EVENT_MOUSE_UP,
    CHITRA_EVENT_MOUSE_SCROLL,
    CHITRA_EVENT_KEY_DOWN,
    CHITRA_EVENT_KEY_UP,
    CHITRA_EVENT_CHAR,
    CHITRA_EVENT_RESIZED
} Chitra_EventType;

typedef struct {
    Chitra_EventType type;
    float            mouse_x;
    float            mouse_y;
    float            scroll_x;
    float            scroll_y;
    int              button;         /* 0: Left, 1: Right, 2: Middle */
    int              key_code;       /* Platform key code */
    uint32_t         char_code;      /* UTF-8 codepoint */
    int              window_width;
    int              window_height;
} Chitra_InputEvent;

/* =========================================================================
 * Standalone Engine Configuration & Lifecycle
 * ========================================================================= */

typedef struct Chitra_Config {
    int         width;
    int         height;
    const char* title;
    int         target_fps; /* 0 = VSync, >0 = manual cap with high-res timer */
    void        (*on_init)(void);
    void        (*on_frame)(void);
    void        (*on_event)(const Chitra_InputEvent* event);
    void        (*on_cleanup)(void);

    /* Optional Engine & GPU Resource Budget Overrides (0 = compact defaults) */
    int         uniform_buffer_size; /* 0 = default 128KB */
    int         max_buffers;         /* 0 = default 32 */
} Chitra_Config;

/* Standalone execution loop */
void chitra_run(const Chitra_Config* config);
void chitra_quit(void);

/* Embedded execution loop */
void chitra_init(int initial_width, int initial_height);
void chitra_begin_frame(int framebuffer_w, int framebuffer_h, float dpi_scale);
void chitra_render(void);
void chitra_end_frame(void);
void chitra_cleanup(void);

/* Frame Pacing */
void chitra_mark_dirty(void);

/* =========================================================================
 * Reservation-First Element Lifecycle
 * ========================================================================= */

uint32_t chitra_create(uint8_t pipeline);
void     chitra_destroy(uint32_t handle);
bool     chitra_is_alive(uint32_t handle);

/* Layering, Clipping, and Texturing */
void chitra_set_draw_order(uint32_t handle, uint32_t draw_order);
void chitra_set_clip(uint32_t handle, uint16_t clip_id);
void chitra_set_texture(uint32_t handle, uint16_t texture_id);

/* =========================================================================
 * Texture Management & Primitive API
 * ========================================================================= */

uint16_t chitra_create_texture_rgba(int width, int height);
void     chitra_update_texture_rgba(uint16_t id, const void* pixels, int width, int height);
void     chitra_destroy_texture(uint16_t id);

bool     chitra_texture_set_bounds(uint32_t handle, float x, float y, float w, float h);
bool     chitra_texture_set_uv(uint32_t handle, float u0, float v0, float u1, float v1);
bool     chitra_texture_set_color(uint32_t handle, uint32_t tint_color);

/* =========================================================================
 * Shape Primitive Setters (O(1) Direct RAM Writes)
 * ========================================================================= */

bool chitra_rect_set_bounds(uint32_t handle, float x, float y, float w, float h);
bool chitra_rect_set_radius(uint32_t handle, float radius);
bool chitra_rect_set_border(uint32_t handle, float border);
bool chitra_rect_set_fill_color(uint32_t handle, uint32_t fill_color);
bool chitra_rect_set_border_color(uint32_t handle, uint32_t border_color);
bool chitra_rect_set_color(uint32_t handle, uint32_t fill_color, uint32_t border_color);

/* =========================================================================
 * Typography API
 * ========================================================================= */

int  chitra_load_font(const char* name, const char* path);
void chitra_unload_font(int font_id);
void chitra_measure_text(int font_id, float size, const char* text, float* out_width, float* out_height);
void chitra_measure_text_wrapped(int font_id, float size, const char* text, float max_width, float* out_width, float* out_height);
void chitra_get_font_metrics(int font_id, float size, float* ascender, float* descender, float* line_height);

/* Element Text Generation (Discards string immediately, defaults to white 0xFFFFFFFF) */
bool chitra_text_set(uint32_t handle, int font_id, float size, const char* text, float x, float y);
bool chitra_text_set_wrapped(uint32_t handle, int font_id, float size, const char* text, float max_width, float x, float y);

/* In-Place Direct Vertex Operations (Pure RAM, 0 string lookup, 0 font engine cost) */
bool chitra_text_set_color(uint32_t handle, uint32_t color);
bool chitra_text_set_pos(uint32_t handle, float x, float y);
bool chitra_text_set_letter_spacing(uint32_t handle, float spacing);
bool chitra_text_get_bounds(uint32_t handle, float* out_width, float* out_height);

/* =========================================================================
 * Scissor Clipping API
 * ========================================================================= */

uint16_t chitra_register_clip(int x, int y, int w, int h);
void     chitra_update_clip(uint16_t clip_id, int x, int y, int w, int h);
void     chitra_destroy_clip(uint16_t clip_id);

#ifdef __cplusplus
}
#endif

#endif /* CHITRA_H */
