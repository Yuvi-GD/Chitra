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
    int (*load_font)(const char* path);
    // Text measurement and rasterization functions will be added here
} Chitra_Font_API;

const Chitra_Font_API* chitra_font_get_api(void);

#endif // CHITRA_FONT_H
