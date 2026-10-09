#ifndef CHITRA_H
#define CHITRA_H

#include <stdint.h>
#include <stdbool.h>

/* =========================================================================
 * Vertex Structures
 * ========================================================================= */

/*
 * 40-byte Shape Vertex
 * Every rectangle, rounded box, pill, button, and border uses this struct.
 * Four vertices form a single quad.
 */
typedef struct {
    float pos[2];           /* 8 bytes: X, Y screen coordinates */
    float uv[2];            /* 8 bytes: UV coordinates (0.0 to 1.0 across the box) */
    float size[2];          /* 8 bytes: Physical width and height of the box */
    float radius_border[2]; /* 8 bytes: Corner radius, border thickness */
    uint32_t fill_color;    /* 4 bytes: Packed RGBA8 (UBYTE4N) */
    uint32_t border_color;  /* 4 bytes: Packed RGBA8 (UBYTE4N) */
} Chitra_ShapeVertex;

/*
 * 20-byte Text/Image Vertex
 * Used for rendering text glyphs from the font atlas, UI images,
 * video frames, and embedded 3D scene viewports.
 */
typedef struct {
    float pos[2];           /* 8 bytes: X, Y screen coordinates */
    float uv[2];            /* 8 bytes: UV coordinates inside the atlas or texture */
    uint32_t color;         /* 4 bytes: Packed RGBA8 tint (UBYTE4N) */
} Chitra_TextVertex;

/* =========================================================================
 * Clip Region
 * ========================================================================= */

typedef struct {
    int x, y, w, h;
} Chitra_ClipRect;

/* =========================================================================
 * Pipeline & Draw Command (Zero Raw Pointers)
 * ========================================================================= */

typedef enum {
    CHITRA_PIPELINE_SHAPE = 0,
    CHITRA_PIPELINE_TEXTURE = 1
} Chitra_PipelineType;

/*
 * Draw command stored in the command arena.
 * Uses integer offsets into vertex/index arenas, never raw pointers.
 */
typedef struct {
    Chitra_PipelineType pipeline; /* SHAPE or TEXTURE */
    uint16_t clip_id;             /* Scissor table index */
    uint16_t texture_id;          /* 0 for shape, atlas ID for text */
    uint32_t vertex_offset;       /* Byte offset inside the vertex arena */
    uint32_t index_offset;        /* Byte offset inside the index arena */
    uint32_t elem_count;          /* Number of indices to draw */
} Chitra_DrawCmd;

/*
 * A merged batch ready for the GPU.
 * Produced by the batcher after sorting and merging compatible DrawCmds.
 */
typedef struct {
    Chitra_PipelineType pipeline;
    uint16_t clip_id;
    uint16_t texture_id;
    uint32_t base_element;        /* Starting index offset (in elements, not bytes) */
    uint32_t num_elements;        /* Total number of indices to draw */
} Chitra_DrawBatch;

/* =========================================================================
 * Chitra Engine API
 * ========================================================================= */

typedef struct {
    int width;
    int height;
    const char* title;
    /* Target FPS (frame cap).
     * 0 = VSync (typically capped at the monitor refresh rate).
     * Positive values disable VSync and apply a manual cap. The manual cap
     * waits in Sokol's frame callback, so event processing can be delayed by
     * up to one target frame interval.
     */
    int target_fps;
    void (*on_init)(void);
    void (*on_frame)(void);
    void (*on_event)(const void* event);
} Chitra_Config;

/* Initializes and runs the standalone Chitra engine. */
void chitra_run(const Chitra_Config* config);

/* =========================================================================
 * Chitra Drawing API (called by the consumer during on_frame)
 * ========================================================================= */

/* Push a rectangle/rounded-box/border into the frame command queue. */
void chitra_draw_rect(int z_index, float x, float y, float w, float h,
                      float radius, float border,
                      uint32_t fill_color, uint32_t border_color);

/* Push pre-built text vertices into the frame command queue. */
void chitra_draw_text_vertices(int z_index, uint16_t texture_id,
                               const Chitra_TextVertex* vertices, int quad_count);

/* Load a TTF font from a file and return its font_id. Returns -1 on failure. */
int chitra_load_font(const char* name, const char* path);

/* Draw a string of text at the given position. */
void chitra_draw_text(int z_index, int font_id, float size, float x, float y, const char* text, uint32_t color);

/* Measure the width and line-height of a string of text. */
void chitra_measure_text(int font_id, float size, const char* text, float* out_width, float* out_height);

/* Push/pop a hardware scissor clipping region. */
void chitra_push_clip(int x, int y, int w, int h);
void chitra_pop_clip(void);

/* Mark the frame as dirty (forces re-upload to GPU). */
void chitra_mark_dirty(void);

/* Clear all commands for the current frame (call before re-pushing). */
void chitra_cmd_clear(void);

#endif /* CHITRA_H */
