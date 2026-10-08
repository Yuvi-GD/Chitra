#ifndef CHITRA_RHI_H
#define CHITRA_RHI_H

#include "chitra.h"
#include <stdint.h>

/* Render Hardware Interface for GPU calls */
typedef struct {
    void (*init)(void);
    void (*begin_frame)(int width, int height);
    void (*end_frame)(void);
    void (*set_scissor)(int x, int y, int w, int h);
    void (*draw_rect)(const Chitra_ShapeVertex* vertices, int count);
    void (*cleanup)(void);
} Chitra_RHI_API;

/* Backend registration (implemented by whatever backend is compiled) */
const Chitra_RHI_API* chitra_rhi_get_api(void);

#endif // CHITRA_RHI_H
