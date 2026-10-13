#include "chitra_texture.h"
#include "chitra_table.h"

/* =========================================================================
 * Texture Primitive Setters (Pure O(1) Direct RAM Writes)
 * ========================================================================= */

bool chitra_texture_set_bounds(uint32_t handle, float x, float y, float w, float h) {
    uint8_t pipe;
    uint32_t count;
    Chitra_TextVertex* v = (Chitra_TextVertex*)chitra_table_get_vertices(handle, &pipe, &count);
    if (!v || pipe != CHITRA_PIPELINE_TEXTURE || count != 4) return false;

    /* Top-Left */
    v[0].pos[0] = x;     v[0].pos[1] = y;
    /* Top-Right */
    v[1].pos[0] = x + w; v[1].pos[1] = y;
    /* Bottom-Right */
    v[2].pos[0] = x + w; v[2].pos[1] = y + h;
    /* Bottom-Left */
    v[3].pos[0] = x;     v[3].pos[1] = y + h;

    chitra_table_mark_dirty();
    return true;
}

bool chitra_texture_set_uv(uint32_t handle, float u0, float v0, float u1, float v1) {
    uint8_t pipe;
    uint32_t count;
    Chitra_TextVertex* v = (Chitra_TextVertex*)chitra_table_get_vertices(handle, &pipe, &count);
    if (!v || pipe != CHITRA_PIPELINE_TEXTURE || count != 4) return false;

    v[0].uv[0] = u0; v[0].uv[1] = v0;
    v[1].uv[0] = u1; v[1].uv[1] = v0;
    v[2].uv[0] = u1; v[2].uv[1] = v1;
    v[3].uv[0] = u0; v[3].uv[1] = v1;

    chitra_table_mark_dirty();
    return true;
}

bool chitra_texture_set_color(uint32_t handle, uint32_t tint_color) {
    uint8_t pipe;
    uint32_t count;
    Chitra_TextVertex* v = (Chitra_TextVertex*)chitra_table_get_vertices(handle, &pipe, &count);
    if (!v || pipe != CHITRA_PIPELINE_TEXTURE || count != 4) return false;

    v[0].color = tint_color;
    v[1].color = tint_color;
    v[2].color = tint_color;
    v[3].color = tint_color;

    chitra_table_mark_dirty();
    return true;
}
