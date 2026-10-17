#include "chitra_clip.h"
#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * Internal State: Dynamic Clip Registry & Recycler Stack
 * ========================================================================= */

typedef struct {
    Chitra_ClipRect rect;
    uint32_t        ref_count;
} Chitra_ClipEntry;

static Chitra_ClipEntry* s_clip_table         = NULL;
static uint32_t          s_clip_count         = 0;
static uint32_t          s_clip_capacity      = 0;

static uint16_t*         s_clip_free_list     = NULL;
static uint32_t          s_clip_free_count    = 0;
static uint32_t          s_clip_free_capacity = 0;

#define CHITRA_INIT_CLIP_CAPACITY 16

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

void chitra_clip_init(void) {
    s_clip_capacity = CHITRA_INIT_CLIP_CAPACITY;
    s_clip_table = (Chitra_ClipEntry*)malloc(s_clip_capacity * sizeof(Chitra_ClipEntry));

    /* Slot 0 is always the default unclipped full-screen viewport (pinned) */
    s_clip_table[0] = (Chitra_ClipEntry){
        .rect = {0, 0, 32767, 32767},
        .ref_count = 1
    };
    s_clip_count = 1;

    s_clip_free_capacity = CHITRA_INIT_CLIP_CAPACITY;
    s_clip_free_list = (uint16_t*)malloc(s_clip_free_capacity * sizeof(uint16_t));
    s_clip_free_count = 0;
}

void chitra_clip_term(void) {
    if (s_clip_table) {
        free(s_clip_table);
        s_clip_table = NULL;
    }
    s_clip_capacity = 0;
    s_clip_count = 0;

    if (s_clip_free_list) {
        free(s_clip_free_list);
        s_clip_free_list = NULL;
    }
    s_clip_free_capacity = 0;
    s_clip_free_count = 0;
}

void chitra_clip_clear(void) {
    /* Reset count, retaining slot 0 as default full-screen viewport */
    s_clip_table[0] = (Chitra_ClipEntry){
        .rect = {0, 0, 32767, 32767},
        .ref_count = 1
    };
    s_clip_count = 1;
    s_clip_free_count = 0;
}

/* =========================================================================
 * Dynamic Growth
 * ========================================================================= */

static void ensure_clip_capacity(uint32_t needed) {
    if (needed > s_clip_capacity) {
        uint32_t new_cap = s_clip_capacity + (s_clip_capacity >> 1); /* 1.5x growth */
        if (new_cap < needed) new_cap = needed + 16;
        if (new_cap > CHITRA_MAX_CLIPS) new_cap = CHITRA_MAX_CLIPS;

        s_clip_table = (Chitra_ClipEntry*)realloc(s_clip_table, new_cap * sizeof(Chitra_ClipEntry));
        s_clip_capacity = new_cap;
    }
}

static void ensure_free_list_capacity(void) {
    if (s_clip_free_count >= s_clip_free_capacity) {
        uint32_t new_cap = s_clip_free_capacity + (s_clip_free_capacity >> 1);
        if (new_cap < CHITRA_INIT_CLIP_CAPACITY) new_cap = CHITRA_INIT_CLIP_CAPACITY;

        uint16_t* new_list = (uint16_t*)realloc(s_clip_free_list, new_cap * sizeof(uint16_t));
        if (new_list) {
            s_clip_free_list = new_list;
            s_clip_free_capacity = new_cap;
        }
    }
}

/* =========================================================================
 * Clip Registration, Mutation, and Reference Counting
 * ========================================================================= */

uint16_t chitra_register_clip(int x, int y, int w, int h) {
    uint16_t id;

    /* Pop recycled clip ID if available, else expand table */
    if (s_clip_free_count > 0) {
        id = s_clip_free_list[--s_clip_free_count];
    } else {
        if (s_clip_count >= CHITRA_MAX_CLIPS) {
            return 0; /* Fallback to default unclipped viewport */
        }
        ensure_clip_capacity(s_clip_count + 1);
        id = (uint16_t)s_clip_count++;
    }

    s_clip_table[id] = (Chitra_ClipEntry){
        .rect = {x, y, w, h},
        .ref_count = 0
    };
    return id;
}

void chitra_update_clip(uint16_t clip_id, int x, int y, int w, int h) {
    if (clip_id < s_clip_count) {
        s_clip_table[clip_id].rect = (Chitra_ClipRect){x, y, w, h};
    }
}

void chitra_retain_clip(uint16_t clip_id) {
    if (clip_id == 0 || clip_id >= s_clip_count) {
        return; /* Slot 0 is unclipped viewport and pinned forever */
    }
    s_clip_table[clip_id].ref_count++;
}

void chitra_release_clip(uint16_t clip_id) {
    if (clip_id == 0 || clip_id >= s_clip_count) {
        return; /* Slot 0 is unclipped viewport and pinned forever */
    }

    Chitra_ClipEntry* entry = &s_clip_table[clip_id];
    if (entry->ref_count > 0) {
        entry->ref_count--;
    }

    /* Auto-delete when reference count reaches 0 */
    if (entry->ref_count == 0) {
        entry->rect = (Chitra_ClipRect){0, 0, 0, 0};
        ensure_free_list_capacity();
        s_clip_free_list[s_clip_free_count++] = clip_id;
    }
}

uint16_t chitra_clip_intersect(uint16_t parent_id, int x, int y, int w, int h) {
    const Chitra_ClipRect* p = chitra_get_clip(parent_id);

    int x1 = x > p->x ? x : p->x;
    int y1 = y > p->y ? y : p->y;

    int x2_new = x + w;
    int x2_par = p->x + p->w;
    int y2_new = y + h;
    int y2_par = p->y + p->h;

    int x2 = x2_new < x2_par ? x2_new : x2_par;
    int y2 = y2_new < y2_par ? y2_new : y2_par;

    int final_w = x2 > x1 ? x2 - x1 : 0;
    int final_h = y2 > y1 ? y2 - y1 : 0;

    return chitra_register_clip(x1, y1, final_w, final_h);
}

/* =========================================================================
 * Accessors & Coordinate Translation
 * ========================================================================= */

const Chitra_ClipRect* chitra_get_clip(uint16_t clip_id) {
    if (clip_id < s_clip_count) {
        return &s_clip_table[clip_id].rect;
    }
    return &s_clip_table[0].rect;
}

Chitra_ClipRect chitra_clip_to_physical(const Chitra_ClipRect* logical, float dpi_scale) {
    if (!logical) {
        return (Chitra_ClipRect){0, 0, 32767, 32767};
    }

    return (Chitra_ClipRect){
        .x = (int)(logical->x * dpi_scale + 0.5f),
        .y = (int)(logical->y * dpi_scale + 0.5f),
        .w = (int)(logical->w * dpi_scale + 0.5f),
        .h = (int)(logical->h * dpi_scale + 0.5f)
    };
}
