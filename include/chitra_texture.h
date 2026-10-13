#ifndef CHITRA_TEXTURE_H
#define CHITRA_TEXTURE_H

#include "chitra.h"
#include <stdint.h>
#include <stdbool.h>

/* Texture Primitive Setters */
bool chitra_texture_set_bounds(uint32_t handle, float x, float y, float w, float h);
bool chitra_texture_set_uv(uint32_t handle, float u0, float v0, float u1, float v1);
bool chitra_texture_set_color(uint32_t handle, uint32_t tint_color);

#endif /* CHITRA_TEXTURE_H */
