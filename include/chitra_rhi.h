#ifndef CHITRA_RHI_H
#define CHITRA_RHI_H

#include "chitra.h"
#include <stdint.h>
#include <stddef.h>

/* =========================================================================
 * Render Hardware Interface (RHI)
 * ========================================================================= */

typedef struct {
    void (*init)(void);
    
    void (*begin_frame)(int width, int height);
    
    /* 
     * Upload all vertex and index data for the current frame.
     * Only called when the UI is dirty to achieve retained-mode efficiency.
     */
    void (*upload_vertices)(const void* shape_data, size_t shape_size,
                            const void* text_data, size_t text_size,
                            const uint16_t* index_data, size_t index_size);
    
    /* Set hardware scissor clipping region. */
    void (*set_scissor)(int x, int y, int w, int h);
    
    /* Dispatch a pre-merged batch of elements. */
    void (*draw_batch)(const Chitra_DrawBatch* batch);
    
    void (*end_frame)(void);
    
    void (*cleanup)(void);
} Chitra_RHI_API;

/* Backend registration (implemented by whatever backend is compiled) */
const Chitra_RHI_API* chitra_rhi_get_api(void);

#endif /* CHITRA_RHI_H */
