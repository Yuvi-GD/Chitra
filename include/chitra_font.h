#ifndef CHITRA_FONT_H
#define CHITRA_FONT_H

#include "chitra.h"

/* 
 * Text Shaping and Rasterization V-Table.
 * Designed to support Fontstash (default) and eventually Freetype+Harfbuzz
 * without changing the core Chitra render pipeline.
 */
typedef struct {
    void (*init)(void);
    void (*term)(void);
    int (*load_font)(const char* name, const char* path);
    
    /* Text layout and iteration */
    void (*push_text)(int z_index, int font_id, float size, float x, float y, const char* text, uint32_t color);
    void (*measure_text)(int font_id, float size, const char* text, float* out_width, float* out_height);
    
    /* Upload dirty atlas data to RHI */
    void (*prepare)(void);
} Chitra_Font_API;

const Chitra_Font_API* chitra_font_get_api(void);

#endif // CHITRA_FONT_H
