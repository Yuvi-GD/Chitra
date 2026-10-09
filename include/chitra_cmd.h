#ifndef CHITRA_CMD_H
#define CHITRA_CMD_H

#include "chitra.h"
#include "chitra_arena.h"

#define CHITRA_MAX_CLIP_DEPTH 64

/* =========================================================================
 * Internal State (opaque to the consumer)
 * ========================================================================= */

/* Initializes all internal arenas and the clip table. */
void chitra_cmd_init(void);

/* Frees all internal arenas. */
void chitra_cmd_term(void);

/* Clears all arenas and resets the clip stack for a new frame. */
void chitra_cmd_clear(void);

/* =========================================================================
 * Drawing Functions (write vertices + indices + commands into arenas)
 * ========================================================================= */

/* Push a shape quad (4 vertices, 6 indices, 1 DrawCmd). */
void chitra_cmd_push_rect(int z_index, float x, float y, float w, float h,
                          float radius, float border, uint32_t fill_color, uint32_t border_color);

/* Push pre-built text quads (N*4 vertices, N*6 indices, 1 DrawCmd). */
void chitra_cmd_push_text(int z_index, uint16_t texture_id, const Chitra_TextVertex* vertices, int quad_count);

/* =========================================================================
 * Clip Stack
 * ========================================================================= */

/* Push a new clip region onto the stack. Returns the clip_id. */
uint16_t chitra_cmd_push_clip(int x, int y, int w, int h);

/* Pop the most recent clip region. */
void chitra_cmd_pop_clip(void);

/* =========================================================================
 * Sort, Batch, and Dispatch
 * ========================================================================= */

/* Returns true if new commands have been pushed since the last flush. */
bool chitra_cmd_is_dirty(void);

/* Mark the command buffer as dirty (forces re-upload next flush). */
void chitra_cmd_set_dirty(void);

/*
 * Sort commands via 64-bit integer keys, merge compatible batches,
 * and upload to the GPU. Called BEFORE RHI begin_frame.
 */
void chitra_cmd_prepare(void);

/*
 * Dispatch batches to the RHI. Called INSIDE RHI begin_frame.
 */
void chitra_cmd_draw(void);

/* =========================================================================
 * Retained-Mode Accessors (used by the RHI to replay idle frames)
 * ========================================================================= */

/* Get the raw arena data for GPU upload. */
const void* chitra_cmd_get_shape_vertices(size_t* out_size);
const void* chitra_cmd_get_text_vertices(size_t* out_size);
const void* chitra_cmd_get_indices(size_t* out_size);

/* Get the sorted batch list for GPU dispatch. */
const Chitra_DrawBatch* chitra_cmd_get_batches(int* out_count);

/* Get a clip rect by ID. */
const Chitra_ClipRect* chitra_cmd_get_clip(uint16_t clip_id);

#endif /* CHITRA_CMD_H */
