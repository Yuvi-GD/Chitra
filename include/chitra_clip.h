#ifndef CHITRA_CLIP_H
#define CHITRA_CLIP_H

#include "chitra.h"
#include <stdint.h>
#include <stdbool.h>

#define CHITRA_MAX_CLIPS 65536

/* Subsystem Lifecycle */
void chitra_clip_init(void);
void chitra_clip_term(void);
void chitra_clip_clear(void);

/* Clip Registration, Mutation, and Reference Counting */
uint16_t chitra_register_clip(int x, int y, int w, int h);
void     chitra_update_clip(uint16_t clip_id, int x, int y, int w, int h);
void     chitra_retain_clip(uint16_t clip_id);
void     chitra_release_clip(uint16_t clip_id);
uint16_t chitra_clip_intersect(uint16_t parent_id, int x, int y, int w, int h);

/* Query & High-DPI Coordinate Mapping */
const Chitra_ClipRect* chitra_get_clip(uint16_t clip_id);
Chitra_ClipRect        chitra_clip_to_physical(const Chitra_ClipRect* logical, float dpi_scale);

#endif /* CHITRA_CLIP_H */
