#ifndef CHITRA_RHI_H
#define CHITRA_RHI_H

#include "chitra_table.h"
#include "chitra_batch.h"
#include <stdint.h>
#include <stddef.h>

/* =========================================================================
 * Render Hardware Interface (RHI) Specification
 * ========================================================================= */

struct Chitra_Config;

typedef struct {
    void (*init)(const struct Chitra_Config* config);
    void (*begin_frame)(int width, int height);

    /* Upload per-pipeline vertex data to GPU VRAM */
    void (*upload_vertices)(uint8_t pipeline, const void* data, size_t size);

    /* Upload unified 32-bit index buffer (SG_INDEXTYPE_UINT32) */
    void (*upload_indices)(const uint32_t* index_data, size_t index_size);

    /* Set hardware scissor clipping region */
    void (*set_scissor)(int x, int y, int w, int h);

    /* Dispatch a pre-merged batch of elements */
    void (*draw_batch)(const Chitra_DrawBatch* batch);

    /* Texture management */
    uint16_t (*create_texture)(int width, int height);
    void     (*update_texture)(uint16_t id, const unsigned char* data, int width, int height);
    uint16_t (*create_texture_rgba)(int width, int height);
    void     (*update_texture_rgba)(uint16_t id, const void* data, int width, int height);
    void     (*destroy_texture)(uint16_t id);

    void (*end_frame)(void);
    void (*cleanup)(void);
} Chitra_RHI_API;

/* Backend registration */
const Chitra_RHI_API* chitra_rhi_get_api(void);

#endif /* CHITRA_RHI_H */
