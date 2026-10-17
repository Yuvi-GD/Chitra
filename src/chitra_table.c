#include "chitra_table.h"
#include "chitra_texture.h"
#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * Internal State: Central Slot Table & Per-Pipeline Staging Arrays
 * ========================================================================= */

static Chitra_CommandTable s_cmd_table = {0};
static Chitra_FreeList     s_free_list = {0};
static Chitra_PipelineData s_pipelines[CHITRA_PIPELINE_COUNT] = {0};
static bool                s_table_dirty = false;

/* Initial capacities for dynamic structures (no upfront RAM waste) */
#define CHITRA_INIT_SLOT_CAPACITY    64
#define CHITRA_INIT_FREE_CAPACITY    32
#define CHITRA_INIT_VERTEX_CAPACITY 256

/* =========================================================================
 * Internal Helpers: Sort Key Packing & Unpacking
 * ========================================================================= */

static inline uint64_t pack_sort_key(uint32_t draw_order, uint16_t clip_id, uint8_t pipeline, uint16_t texture_id) {
    return (((uint64_t)(draw_order & CHITRA_DRAW_ORDER_MASK)) << 34)
         | (((uint64_t)(clip_id    & CHITRA_CLIP_ID_MASK))    << 18)
         | (((uint64_t)(pipeline   & CHITRA_PIPELINE_MASK))   << 13)
         |  ((uint64_t)(texture_id & CHITRA_TEXTURE_ID_MASK));
}

static inline uint8_t unpack_pipeline(uint64_t key) {
    return (uint8_t)((key >> 13) & CHITRA_PIPELINE_MASK);
}

/* =========================================================================
 * Subsystem Lifecycle
 * ========================================================================= */

void chitra_table_init(void) {
    /* 1. Command Table (Single Contiguous Allocation) */
    s_cmd_table.capacity = CHITRA_INIT_SLOT_CAPACITY;
    s_cmd_table.count = 0;
    s_cmd_table.slots = (Chitra_CommandSlot*)calloc(s_cmd_table.capacity, sizeof(Chitra_CommandSlot));

    /* 2. Free-List Recycler Stack */
    s_free_list.capacity = CHITRA_INIT_FREE_CAPACITY;
    s_free_list.count = 0;
    s_free_list.ids = (uint32_t*)malloc(s_free_list.capacity * sizeof(uint32_t));

    /* 3. Central Staging Arrays per Pipeline */
    s_pipelines[CHITRA_PIPELINE_SHAPE].stride   = sizeof(Chitra_ShapeVertex); /* 40 bytes */
    s_pipelines[CHITRA_PIPELINE_TEXT].stride    = sizeof(Chitra_TextVertex);  /* 20 bytes */
    s_pipelines[CHITRA_PIPELINE_TEXTURE].stride = sizeof(Chitra_TextVertex);  /* 20 bytes */

    for (int i = 0; i < CHITRA_PIPELINE_COUNT; i++) {
        Chitra_PipelineData* pipe = &s_pipelines[i];
        pipe->cursor = 0;
        pipe->dead_count = 0;
        pipe->capacity = CHITRA_INIT_VERTEX_CAPACITY;
        pipe->data = malloc(pipe->capacity * pipe->stride);
    }

    s_table_dirty = false;
}

void chitra_table_term(void) {
    if (s_cmd_table.slots) {
        free(s_cmd_table.slots);
        s_cmd_table.slots = NULL;
    }
    s_cmd_table.count = 0;
    s_cmd_table.capacity = 0;

    if (s_free_list.ids) {
        free(s_free_list.ids);
        s_free_list.ids = NULL;
    }
    s_free_list.count = 0;
    s_free_list.capacity = 0;

    for (int i = 0; i < CHITRA_PIPELINE_COUNT; i++) {
        if (s_pipelines[i].data) {
            free(s_pipelines[i].data);
            s_pipelines[i].data = NULL;
        }
        s_pipelines[i].cursor = 0;
        s_pipelines[i].capacity = 0;
        s_pipelines[i].dead_count = 0;
    }

    s_table_dirty = false;
}

void chitra_table_clear_all(void) {
    s_cmd_table.count = 0;
    s_free_list.count = 0;

    for (int i = 0; i < CHITRA_PIPELINE_COUNT; i++) {
        s_pipelines[i].cursor = 0;
        s_pipelines[i].dead_count = 0;
    }

    s_table_dirty = true;
}

/* =========================================================================
 * Internal Capacity Management (Single Reallocation)
 * ========================================================================= */

static void ensure_slot_capacity(void) {
    if (s_cmd_table.count >= s_cmd_table.capacity) {
        uint32_t new_cap = s_cmd_table.capacity + (s_cmd_table.capacity >> 1); /* 1.5x growth */
        if (new_cap < CHITRA_INIT_SLOT_CAPACITY) new_cap = CHITRA_INIT_SLOT_CAPACITY;

        Chitra_CommandSlot* new_slots = (Chitra_CommandSlot*)realloc(
            s_cmd_table.slots, new_cap * sizeof(Chitra_CommandSlot)
        );

        if (new_slots) {
            memset(&new_slots[s_cmd_table.capacity], 0, (new_cap - s_cmd_table.capacity) * sizeof(Chitra_CommandSlot));
            s_cmd_table.slots = new_slots;
            s_cmd_table.capacity = new_cap;
        }
    }
}

static void ensure_free_list_capacity(void) {
    if (s_free_list.count >= s_free_list.capacity) {
        uint32_t new_cap = s_free_list.capacity + (s_free_list.capacity >> 1); /* 1.5x growth */
        if (new_cap < CHITRA_INIT_FREE_CAPACITY) new_cap = CHITRA_INIT_FREE_CAPACITY;

        uint32_t* new_ids = (uint32_t*)realloc(s_free_list.ids, new_cap * sizeof(uint32_t));
        if (new_ids) {
            s_free_list.ids = new_ids;
            s_free_list.capacity = new_cap;
        }
    }
}

static void ensure_vertex_capacity(Chitra_PipelineData* pipe, uint32_t needed) {
    if (pipe->cursor + needed > pipe->capacity) {
        uint32_t new_cap = (uint32_t)(pipe->capacity * 1.75f); /* 1.75x growth */
        if (new_cap < pipe->cursor + needed) {
            new_cap = pipe->cursor + needed + 128;
        }

        void* new_data = realloc(pipe->data, new_cap * pipe->stride);
        if (new_data) {
            pipe->data = new_data;
            pipe->capacity = new_cap;
        }
    }
}

/* =========================================================================
 * Central Staging Memory Allocation
 * ========================================================================= */

void* chitra_table_alloc_vertices(uint8_t pipeline, uint32_t handle, uint32_t count) {
    if (handle >= s_cmd_table.count || pipeline >= CHITRA_PIPELINE_COUNT) return NULL;
    Chitra_CommandSlot* slot = &s_cmd_table.slots[handle];
    if (!chitra_slot_is_alive(slot)) return NULL;

    Chitra_PipelineData* pipe = &s_pipelines[pipeline];
    uint32_t old_count = chitra_slot_vertex_count(slot);

    if (count <= old_count) {
        /* In-place shrink / overwrite: mark trailing vertices as dead */
        pipe->dead_count += (old_count - count);
        chitra_slot_set_vertex_count(slot, count);
        s_table_dirty = true;
        return (uint8_t*)pipe->data + (slot->start_vertex_index * pipe->stride);
    }

    /* Growth: abandon old geometry as dead hole, append at cursor */
    if (old_count > 0) {
        pipe->dead_count += old_count;
    }

    ensure_vertex_capacity(pipe, count);
    slot->start_vertex_index = pipe->cursor;
    chitra_slot_set_vertex_count(slot, count);
    pipe->cursor += count;

    s_table_dirty = true;
    return (uint8_t*)pipe->data + (slot->start_vertex_index * pipe->stride);
}

void* chitra_table_get_vertices(uint32_t handle, uint8_t* out_pipeline, uint32_t* out_count) {
    if (handle >= s_cmd_table.count) return NULL;
    Chitra_CommandSlot* slot = &s_cmd_table.slots[handle];
    if (!chitra_slot_is_alive(slot)) return NULL;

    uint8_t pipeline = unpack_pipeline(slot->sort_key);
    uint32_t count = chitra_slot_vertex_count(slot);

    if (out_pipeline) *out_pipeline = pipeline;
    if (out_count)    *out_count = count;

    if (count == 0 || pipeline >= CHITRA_PIPELINE_COUNT) return NULL;
    return (uint8_t*)s_pipelines[pipeline].data + (slot->start_vertex_index * s_pipelines[pipeline].stride);
}

const Chitra_PipelineData* chitra_table_get_pipeline(uint8_t pipeline) {
    if (pipeline >= CHITRA_PIPELINE_COUNT) return NULL;
    return &s_pipelines[pipeline];
}

/* =========================================================================
 * Element Reservation and Destruction
 * ========================================================================= */

uint32_t chitra_create(uint8_t pipeline) {
    if (pipeline >= CHITRA_PIPELINE_COUNT) {
        return CHITRA_INVALID_HANDLE;
    }

    uint32_t handle;

    /* Pop recycled slot from LIFO stack, or append next slot */
    if (s_free_list.count > 0) {
        handle = s_free_list.ids[--s_free_list.count];
    } else {
        ensure_slot_capacity();
        handle = s_cmd_table.count++;
    }

    Chitra_CommandSlot* slot = &s_cmd_table.slots[handle];
    slot->sort_key = pack_sort_key(0, 0, pipeline, 0);
    slot->start_vertex_index = 0;
    slot->vertex_meta = CHITRA_SLOT_ALIVE_BIT; /* Alive = true, Vertex Count = 0 */

    /* Auto-allocate 1 quad (4 vertices) for Shape and Texture pipelines */
    if (pipeline == CHITRA_PIPELINE_SHAPE) {
        void* base = chitra_table_alloc_vertices(CHITRA_PIPELINE_SHAPE, handle, 4);
        if (base) memset(base, 0, 4 * sizeof(Chitra_ShapeVertex));
    } else if (pipeline == CHITRA_PIPELINE_TEXTURE) {
        Chitra_TextVertex* base = (Chitra_TextVertex*)chitra_table_alloc_vertices(CHITRA_PIPELINE_TEXTURE, handle, 4);
        if (base) {
            memset(base, 0, 4 * sizeof(Chitra_TextVertex));
            base[0].uv[0] = 0.0f; base[0].uv[1] = 0.0f;
            base[1].uv[0] = 1.0f; base[1].uv[1] = 0.0f;
            base[2].uv[0] = 1.0f; base[2].uv[1] = 1.0f;
            base[3].uv[0] = 0.0f; base[3].uv[1] = 1.0f;
            base[0].color = 0xFFFFFFFF;
            base[1].color = 0xFFFFFFFF;
            base[2].color = 0xFFFFFFFF;
            base[3].color = 0xFFFFFFFF;
        }
    }

    s_table_dirty = true;
    return handle;
}

void chitra_destroy(uint32_t handle) {
    if (handle >= s_cmd_table.count) return;

    Chitra_CommandSlot* slot = &s_cmd_table.slots[handle];
    if (!chitra_slot_is_alive(slot)) return;

    uint8_t pipeline = unpack_pipeline(slot->sort_key);
    uint32_t count = chitra_slot_vertex_count(slot);

    if (pipeline < CHITRA_PIPELINE_COUNT && count > 0) {
        s_pipelines[pipeline].dead_count += count;
    }

    if (pipeline == CHITRA_PIPELINE_TEXTURE) {
        chitra_texture_on_destroy(handle);
    }

    /* Mark slot dead */
    slot->vertex_meta = 0;

    /* Push handle to LIFO recycler stack */
    ensure_free_list_capacity();
    s_free_list.ids[s_free_list.count++] = handle;

    s_table_dirty = true;
}

bool chitra_is_alive(uint32_t handle) {
    if (handle >= s_cmd_table.count) return false;
    return chitra_slot_is_alive(&s_cmd_table.slots[handle]);
}

Chitra_CommandSlot* chitra_table_get_slot(uint32_t handle) {
    if (handle >= s_cmd_table.count) return NULL;
    return &s_cmd_table.slots[handle];
}

/* =========================================================================
 * Visual State Setters (Sort Key Modifications)
 * ========================================================================= */

void chitra_set_draw_order(uint32_t handle, uint32_t draw_order) {
    if (!chitra_is_alive(handle)) return;
    Chitra_CommandSlot* slot = &s_cmd_table.slots[handle];

    uint64_t masked = slot->sort_key & ~(((uint64_t)CHITRA_DRAW_ORDER_MASK) << 34);
    slot->sort_key = masked | (((uint64_t)(draw_order & CHITRA_DRAW_ORDER_MASK)) << 34);
    s_table_dirty = true;
}

void chitra_set_clip(uint32_t handle, uint16_t clip_id) {
    if (!chitra_is_alive(handle)) return;
    Chitra_CommandSlot* slot = &s_cmd_table.slots[handle];

    uint64_t masked = slot->sort_key & ~(((uint64_t)CHITRA_CLIP_ID_MASK) << 18);
    slot->sort_key = masked | (((uint64_t)(clip_id & CHITRA_CLIP_ID_MASK)) << 18);
    s_table_dirty = true;
}

void chitra_set_texture(uint32_t handle, uint16_t texture_id) {
    if (!chitra_is_alive(handle)) return;
    Chitra_CommandSlot* slot = &s_cmd_table.slots[handle];

    uint64_t masked = slot->sort_key & ~((uint64_t)CHITRA_TEXTURE_ID_MASK);
    slot->sort_key = masked | ((uint64_t)(texture_id & CHITRA_TEXTURE_ID_MASK));
    s_table_dirty = true;
}

/* =========================================================================
 * Runtime Inspection Getters (Introspection & Debugging Only)
 * ========================================================================= */

uint16_t chitra_get_texture_id(uint32_t handle) {
    if (!chitra_is_alive(handle)) return 0;
    const Chitra_CommandSlot* slot = &s_cmd_table.slots[handle];
    return (uint16_t)(slot->sort_key & CHITRA_TEXTURE_ID_MASK);
}

uint16_t chitra_get_clip_id(uint32_t handle) {
    if (!chitra_is_alive(handle)) return 0;
    const Chitra_CommandSlot* slot = &s_cmd_table.slots[handle];
    return (uint16_t)((slot->sort_key >> 18) & CHITRA_CLIP_ID_MASK);
}

uint8_t chitra_get_pipeline_id(uint32_t handle) {
    if (!chitra_is_alive(handle)) return 0;
    const Chitra_CommandSlot* slot = &s_cmd_table.slots[handle];
    return (uint8_t)((slot->sort_key >> 13) & CHITRA_PIPELINE_MASK);
}

uint32_t chitra_get_draw_order(uint32_t handle) {
    if (!chitra_is_alive(handle)) return 0;
    const Chitra_CommandSlot* slot = &s_cmd_table.slots[handle];
    return (uint32_t)((slot->sort_key >> 34) & CHITRA_DRAW_ORDER_MASK);
}

/* =========================================================================
 * Central Compaction Algorithm
 * ========================================================================= */

void chitra_table_compact_pipeline(uint8_t pipeline) {
    if (pipeline >= CHITRA_PIPELINE_COUNT) return;
    Chitra_PipelineData* pipe = &s_pipelines[pipeline];
    if (pipe->dead_count == 0) return;

    uint32_t active_vertices = pipe->cursor - pipe->dead_count;
    uint32_t new_cap = (uint32_t)(active_vertices * 1.75f);
    if (new_cap < CHITRA_INIT_VERTEX_CAPACITY) new_cap = CHITRA_INIT_VERTEX_CAPACITY;

    void* temp_data = malloc(new_cap * pipe->stride);
    if (!temp_data) return;

    uint32_t packed_cursor = 0;

    for (uint32_t i = 0; i < s_cmd_table.count; i++) {
        Chitra_CommandSlot* slot = &s_cmd_table.slots[i];
        if (chitra_slot_is_alive(slot) && unpack_pipeline(slot->sort_key) == pipeline) {
            uint32_t count = chitra_slot_vertex_count(slot);
            if (count > 0) {
                void* src = (uint8_t*)pipe->data + (slot->start_vertex_index * pipe->stride);
                void* dst = (uint8_t*)temp_data + (packed_cursor * pipe->stride);

                memcpy(dst, src, count * pipe->stride);
                slot->start_vertex_index = packed_cursor;
                packed_cursor += count;
            }
        }
    }

    free(pipe->data);
    pipe->data = temp_data;
    pipe->cursor = packed_cursor;
    pipe->capacity = new_cap;
    pipe->dead_count = 0;

    s_table_dirty = true;
}

void chitra_table_check_compaction(uint8_t pipeline, bool is_idle) {
    if (pipeline >= CHITRA_PIPELINE_COUNT) return;
    Chitra_PipelineData* pipe = &s_pipelines[pipeline];

    if (is_idle && pipe->dead_count > (pipe->capacity >> 1)) {
        chitra_table_compact_pipeline(pipeline);
    }
}

/* =========================================================================
 * Accessors
 * ========================================================================= */

const Chitra_CommandTable* chitra_table_get_commands(void) {
    return &s_cmd_table;
}

bool chitra_table_is_dirty(void) {
    return s_table_dirty;
}

void chitra_table_mark_dirty(void) {
    s_table_dirty = true;
}

void chitra_table_clear_dirty(void) {
    s_table_dirty = false;
}
