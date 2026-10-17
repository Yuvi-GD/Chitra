#ifndef CHITRA_FONT_H
#define CHITRA_FONT_H

#include <stdint.h>
#include <stdbool.h>

/* =========================================================================
 * Typography Subsystem V-Table Interface
 * ========================================================================= */

typedef struct {
    void (*init)(void);
    void (*term)(void);
    int  (*load_font)(const char* name, const char* path);
    void (*unload_font)(int font_id);

    /* Pure CPU layout metrics measurement (Zero allocations) */
    void (*measure_text)(int font_id, float size, const char* text, float* out_width, float* out_height);
    void (*measure_text_wrapped)(int font_id, float size, const char* text, float max_width, float* out_width, float* out_height);
    void (*get_font_metrics)(int font_id, float size, float* ascender, float* descender, float* line_height);

    /* Text quad generation directly into Chitra central staging memory (default white color) */
    bool (*set_text)(uint32_t handle, int font_id, float size, const char* text, float x, float y);
    bool (*set_text_wrapped)(uint32_t handle, int font_id, float size, const char* text, float max_width, float x, float y);

    /* In-place vertex operations (O(1) in RAM, 0 string lookup, 0 font engine cost) */
    bool (*set_color)(uint32_t handle, uint32_t color);
    bool (*set_pos)(uint32_t handle, float x, float y);
    bool (*set_opacity)(uint32_t handle, float opacity);
    bool (*set_rotation)(uint32_t handle, float origin_x, float origin_y, float angle_rad);
    bool (*set_letter_spacing)(uint32_t handle, float spacing);
    bool (*get_bounds)(uint32_t handle, float* out_width, float* out_height);

    /* Single-upload atlas synchronization before RHI render pass */
    void (*prepare)(void);
    uint16_t (*get_atlas_texture_id)(void);
} Chitra_Font_API;

const Chitra_Font_API* chitra_font_get_api(void);

#endif /* CHITRA_FONT_H */
