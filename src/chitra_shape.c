#include "chitra_shape.h"
#include "chitra_table.h"
#include <math.h>

/* =========================================================================
 * Shape Primitive Setters (Pure O(1) Direct RAM Writes)
 * ========================================================================= */

bool chitra_rect_set_bounds(uint32_t handle, float x, float y, float w, float h) {
    uint8_t pipe;
    uint32_t count;
    Chitra_ShapeVertex* v = (Chitra_ShapeVertex*)chitra_table_get_vertices(handle, &pipe, &count);
    if (!v || pipe != CHITRA_PIPELINE_SHAPE || count != 4) return false;

    /* Top-Left */
    v[0].pos[0] = x;     v[0].pos[1] = y;
    v[0].uv[0]  = 0.0f;  v[0].uv[1]  = 0.0f;
    v[0].size[0]= w;     v[0].size[1]= h;

    /* Top-Right */
    v[1].pos[0] = x + w; v[1].pos[1] = y;
    v[1].uv[0]  = 1.0f;  v[1].uv[1]  = 0.0f;
    v[1].size[0]= w;     v[1].size[1]= h;

    /* Bottom-Right */
    v[2].pos[0] = x + w; v[2].pos[1] = y + h;
    v[2].uv[0]  = 1.0f;  v[2].uv[1]  = 1.0f;
    v[2].size[0]= w;     v[2].size[1]= h;

    /* Bottom-Left */
    v[3].pos[0] = x;     v[3].pos[1] = y + h;
    v[3].uv[0]  = 0.0f;  v[3].uv[1]  = 1.0f;
    v[3].size[0]= w;     v[3].size[1]= h;

    chitra_table_mark_dirty();
    return true;
}

bool chitra_rect_set_radius(uint32_t handle, float radius) {
    uint8_t pipe;
    uint32_t count;
    Chitra_ShapeVertex* v = (Chitra_ShapeVertex*)chitra_table_get_vertices(handle, &pipe, &count);
    if (!v || pipe != CHITRA_PIPELINE_SHAPE || count != 4) return false;

    v[0].radius_border[0] = radius;
    v[1].radius_border[0] = radius;
    v[2].radius_border[0] = radius;
    v[3].radius_border[0] = radius;

    chitra_table_mark_dirty();
    return true;
}

bool chitra_rect_set_border(uint32_t handle, float border) {
    uint8_t pipe;
    uint32_t count;
    Chitra_ShapeVertex* v = (Chitra_ShapeVertex*)chitra_table_get_vertices(handle, &pipe, &count);
    if (!v || pipe != CHITRA_PIPELINE_SHAPE || count != 4) return false;

    v[0].radius_border[1] = border;
    v[1].radius_border[1] = border;
    v[2].radius_border[1] = border;
    v[3].radius_border[1] = border;

    chitra_table_mark_dirty();
    return true;
}

bool chitra_rect_set_fill_color(uint32_t handle, uint32_t fill_color) {
    uint8_t pipe;
    uint32_t count;
    Chitra_ShapeVertex* v = (Chitra_ShapeVertex*)chitra_table_get_vertices(handle, &pipe, &count);
    if (!v || pipe != CHITRA_PIPELINE_SHAPE || count != 4) return false;

    v[0].fill_color = fill_color;
    v[1].fill_color = fill_color;
    v[2].fill_color = fill_color;
    v[3].fill_color = fill_color;

    chitra_table_mark_dirty();
    return true;
}

bool chitra_rect_set_border_color(uint32_t handle, uint32_t border_color) {
    uint8_t pipe;
    uint32_t count;
    Chitra_ShapeVertex* v = (Chitra_ShapeVertex*)chitra_table_get_vertices(handle, &pipe, &count);
    if (!v || pipe != CHITRA_PIPELINE_SHAPE || count != 4) return false;

    v[0].border_color = border_color;
    v[1].border_color = border_color;
    v[2].border_color = border_color;
    v[3].border_color = border_color;

    chitra_table_mark_dirty();
    return true;
}

bool chitra_rect_set_color(uint32_t handle, uint32_t fill_color, uint32_t border_color) {
    if (!chitra_rect_set_fill_color(handle, fill_color)) return false;
    return chitra_rect_set_border_color(handle, border_color);
}

bool chitra_rect_set_opacity(uint32_t handle, float opacity) {
    uint8_t pipe;
    uint32_t count;
    Chitra_ShapeVertex* v = (Chitra_ShapeVertex*)chitra_table_get_vertices(handle, &pipe, &count);
    if (!v || pipe != CHITRA_PIPELINE_SHAPE || count != 4) return false;

    if (opacity < 0.0f) opacity = 0.0f;
    if (opacity > 1.0f) opacity = 1.0f;

    for (int i = 0; i < 4; i++) {
        uint32_t fc = v[i].fill_color;
        uint8_t fa = (uint8_t)(((fc >> 24) & 0xFF) * opacity);
        v[i].fill_color = (fc & 0x00FFFFFF) | (((uint32_t)fa) << 24);

        uint32_t bc = v[i].border_color;
        uint8_t ba = (uint8_t)(((bc >> 24) & 0xFF) * opacity);
        v[i].border_color = (bc & 0x00FFFFFF) | (((uint32_t)ba) << 24);
    }

    chitra_table_mark_dirty();
    return true;
}

bool chitra_rect_set_rotation(uint32_t handle, float origin_x, float origin_y, float angle_rad) {
    uint8_t pipe;
    uint32_t count;
    Chitra_ShapeVertex* v = (Chitra_ShapeVertex*)chitra_table_get_vertices(handle, &pipe, &count);
    if (!v || pipe != CHITRA_PIPELINE_SHAPE || count != 4) return false;

    float c = cosf(angle_rad);
    float s = sinf(angle_rad);

    for (int i = 0; i < 4; i++) {
        float px = v[i].pos[0] - origin_x;
        float py = v[i].pos[1] - origin_y;
        v[i].pos[0] = origin_x + (px * c - py * s);
        v[i].pos[1] = origin_y + (px * s + py * c);
    }

    chitra_table_mark_dirty();
    return true;
}
