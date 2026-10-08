#ifndef CHITRA_H
#define CHITRA_H

#include <stdint.h>
#include <stdbool.h>

/* =========================================================================
 * Chitra Optimized Vertex Structures
 * ========================================================================= */

/* 
 * 20-byte Base Vertex 
 * Used natively for Text rendering, saving maximum bandwidth.
 * Color is packed as UBYTE4N (4 bytes) instead of float[4] (16 bytes).
 */
typedef struct {
    float pos[2];
    float uv[2];
    uint32_t color; 
} Chitra_BaseVertex;

/* 
 * 36-byte Shape Vertex
 * Extends the base vertex with specific properties for SDF shapes.
 * Hardware scissoring handles clipping, completely removing 'clip' from vertex memory.
 */
typedef struct {
    Chitra_BaseVertex base; 
    float size[2];
    float radius_border[2];
} Chitra_ShapeVertex;

/* =========================================================================
 * Chitra Engine API
 * ========================================================================= */

typedef struct {
    int width;
    int height;
    const char* title;
} Chitra_Config;

/* 
 * Initializes the standalone Chitra engine.
 * Encapsulates window creation (HAL) and rendering (RHI).
 */
void chitra_run(const Chitra_Config* config);

#endif // CHITRA_H
