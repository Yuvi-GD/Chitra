#ifndef CHITRA_TEXTURE_H
#define CHITRA_TEXTURE_H

#include "chitra.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Subsystem Lifecycle */
void chitra_texture_init(void);
void chitra_texture_term(void);
void chitra_texture_prepare(void);
void chitra_texture_on_destroy(uint32_t handle);

/* Unified Texture Loading & Assignment:
 * - If w > 512 or h > 512: Allocates a dedicated Standalone GPU texture (UV = 0..1).
 * - If w <= 512 and h <= 512: Packs into Chitra's dynamic 2048x2048 atlas via stb_rect_pack (UV = atlas sub-rect).
 * - Writes normalized UVs to the 4 vertices and sets texture_id in sort_key. */
bool chitra_texture_load(uint32_t handle, int width, int height, const void* rgba_pixels);
bool chitra_texture_load_file(uint32_t handle, const char* file_path);

/* Spritesheet / Local Rect UV Mapping:
 * Maps a local pixel sub-rectangle (src_x, src_y, src_w, src_h) relative to image size (img_w, img_h).
 * Works identically for Standalone textures and Dynamic Atlas packed regions! */
bool chitra_texture_set_src_rect(uint32_t handle, float src_x, float src_y, float src_w, float src_h, float img_w, float img_h);

/* Texture Primitive Setters */
bool chitra_texture_set_bounds(uint32_t handle, float x, float y, float w, float h);
bool chitra_texture_get_bounds(uint32_t handle, float* out_w, float* out_h);
bool chitra_texture_set_uv(uint32_t handle, float u0, float v0, float u1, float v1);
bool chitra_texture_set_color(uint32_t handle, uint32_t tint_color);

/* Atlas Texture Accessor & Inspection */
uint16_t chitra_texture_get_atlas_id(void);
bool     chitra_texture_is_atlas(uint16_t tex_id);
uint32_t chitra_texture_get_ref_count(uint16_t tex_id);
uint32_t chitra_texture_get_allocated_area(uint16_t tex_id);
uint32_t chitra_texture_get_dead_area(uint16_t tex_id);

#ifdef __cplusplus
}
#endif

#endif /* CHITRA_TEXTURE_H */
