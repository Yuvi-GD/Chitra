#ifndef CHITRA_TABLE_H
#define CHITRA_TABLE_H

#include "chitra.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* =========================================================================
 * Bit Budget Constraints & Bitmasks
 * ========================================================================= */

#define CHITRA_MAX_CLIPS       65536
#define CHITRA_MAX_TEXTURES    8192
#define CHITRA_MAX_DRAW_ORDERS 1073741824ULL /* 2^30 layers */

/* Bitmask definitions for 64-bit visual sort key:
 * [ Draw Order (30 bits) | Clip ID (16 bits) | Pipeline ID (5 bits) | Texture ID (13 bits) ] */
#define CHITRA_DRAW_ORDER_MASK 0x3FFFFFFFULL
#define CHITRA_CLIP_ID_MASK    0xFFFFULL
#define CHITRA_PIPELINE_MASK   0x1FULL
#define CHITRA_TEXTURE_ID_MASK 0x1FFFULL

#define CHITRA_STATE_MASK      0x00000003FFFFFFFFULL /* Bottom 34 bits (Clip, Pipe, Tex) */

/* Bitmask definitions for 32-bit vertex metadata:
 * [ Bit 31: Alive Flag | Bits 0..30: Vertex Count (up to 2,147,483,647 vertices) ] */
#define CHITRA_SLOT_ALIVE_BIT    0x80000000U
#define CHITRA_VERTEX_COUNT_MASK 0x7FFFFFFFU

/* =========================================================================
 * Dynamic 2-Table Memory Structures
 * ========================================================================= */

/* Exactly 16 bytes: 4 slots fit into a single 64-byte CPU cache line */
typedef struct {
    uint64_t sort_key;           /* 8 bytes: Pre-compiled visual state for Radix Sort */
    uint32_t start_vertex_index; /* 4 bytes: Pipeline-local vertex offset in staging array */
    uint32_t vertex_meta;        /* 4 bytes: Bit 31 = Alive Flag, Bits 0..30 = Vertex Count */
} Chitra_CommandSlot;

/* Inlined helper accessors for vertex_meta */
static inline bool chitra_slot_is_alive(const Chitra_CommandSlot* slot) {
    return (slot->vertex_meta & CHITRA_SLOT_ALIVE_BIT) != 0;
}

static inline uint32_t chitra_slot_vertex_count(const Chitra_CommandSlot* slot) {
    return slot->vertex_meta & CHITRA_VERTEX_COUNT_MASK;
}

static inline void chitra_slot_set_vertex_count(Chitra_CommandSlot* slot, uint32_t count) {
    slot->vertex_meta = (slot->vertex_meta & CHITRA_SLOT_ALIVE_BIT) | (count & CHITRA_VERTEX_COUNT_MASK);
}

static inline void chitra_slot_set_alive(Chitra_CommandSlot* slot, bool alive) {
    if (alive) slot->vertex_meta |= CHITRA_SLOT_ALIVE_BIT;
    else       slot->vertex_meta &= ~CHITRA_SLOT_ALIVE_BIT;
}

/* Dynamic slot table storing element metadata in a single contiguous array */
typedef struct {
    Chitra_CommandSlot* slots;
    uint32_t            count;    /* High-water mark of allocated slots */
    uint32_t            capacity; /* Allocated slot capacity (grows 1.5x) */
} Chitra_CommandTable;

/* Dynamic LIFO recycler stack for recycling destroyed handles */
typedef struct {
    uint32_t* ids;
    uint32_t  count;    /* Number of recycled handles available */
    uint32_t  capacity; /* Allocated stack capacity (grows 1.5x) */
} Chitra_FreeList;

/* Central staging array descriptor for pipeline vertex memory */
typedef struct {
    void*    data;       /* Contiguous vertex memory */
    uint32_t stride;     /* Byte stride per vertex (40 for Shape, 20 for others) */
    uint32_t cursor;     /* Total physical vertices written to date */
    uint32_t capacity;   /* Maximum physical vertices before growth/compaction */
    uint32_t dead_count; /* Abandoned/dead vertices (Active = cursor - dead_count) */
} Chitra_PipelineData;

/* =========================================================================
 * Core Memory Subsystem Lifecycle
 * ========================================================================= */

void chitra_table_init(void);
void chitra_table_term(void);
void chitra_table_clear_all(void);

/* Element Allocation and Destruction */
uint32_t chitra_create(uint8_t pipeline);
void     chitra_destroy(uint32_t handle);
bool     chitra_is_alive(uint32_t handle);

/* Slot and Staging Memory Management */
Chitra_CommandSlot*        chitra_table_get_slot(uint32_t handle);
void*                      chitra_table_alloc_vertices(uint8_t pipeline, uint32_t handle, uint32_t count);
void*                      chitra_table_get_vertices(uint32_t handle, uint8_t* out_pipeline, uint32_t* out_count);
const Chitra_PipelineData* chitra_table_get_pipeline(uint8_t pipeline);

/* Central Compaction and Garbage Collection */
void chitra_table_compact_pipeline(uint8_t pipeline);

/* Visual State Modifiers */
void chitra_set_draw_order(uint32_t handle, uint32_t draw_order);
void chitra_set_clip(uint32_t handle, uint16_t clip_id);
void chitra_set_texture(uint32_t handle, uint16_t texture_id);

/* Table Accessors & Dirty State */
const Chitra_CommandTable* chitra_table_get_commands(void);
bool chitra_table_is_dirty(void);
void chitra_table_mark_dirty(void);
void chitra_table_clear_dirty(void);

#endif /* CHITRA_TABLE_H */
