#include "chitra_cmd.h"
#include "chitra_rhi.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* =========================================================================
 * Internal Arenas
 * ========================================================================= */

static Chitra_Arena s_shape_vtx;   /* Chitra_ShapeVertex data */
static Chitra_Arena s_text_vtx;    /* Chitra_TextVertex data */
static Chitra_Arena s_index;       /* uint16_t index data */
static Chitra_Arena s_cmds;        /* Chitra_DrawCmd data */

/* =========================================================================
 * Clip Table & Stack
 * ========================================================================= */

static Chitra_ClipRect s_clip_table[CHITRA_MAX_CLIP_DEPTH];
static uint16_t s_clip_stack[CHITRA_MAX_CLIP_DEPTH];
static int s_clip_table_count = 0;
static int s_clip_stack_depth = 0;

/* =========================================================================
 * Dirty Flag & Batch Cache
 * ========================================================================= */

static bool s_dirty = false;
static int s_cmd_count = 0;
static uint16_t s_shape_vertex_count = 0;
static uint16_t s_text_vertex_count = 0;

/* Cached sorted batch list (persists across idle frames) */
static Chitra_DrawBatch* s_batches = NULL;
static int s_batch_count = 0;

/* =========================================================================
 * Init / Term / Clear
 * ========================================================================= */

void chitra_cmd_init(void) {
    chitra_arena_init(&s_shape_vtx, 128 * 1024);  /* 128KB */
    chitra_arena_init(&s_text_vtx,  128 * 1024);  /* 128KB */
    chitra_arena_init(&s_index,      32 * 1024);  /*  32KB */
    chitra_arena_init(&s_cmds,       32 * 1024);  /*  32KB */
    
    /* Default full-screen clip at slot 0 */
    s_clip_table[0] = (Chitra_ClipRect){0, 0, 16384, 16384};
    s_clip_table_count = 1;
    s_clip_stack[0] = 0;
    s_clip_stack_depth = 1;
    
    s_dirty = false;
    s_cmd_count = 0;
    s_shape_vertex_count = 0;
    s_text_vertex_count = 0;
    s_batches = NULL;
    s_batch_count = 0;
}

void chitra_cmd_term(void) {
    chitra_arena_term(&s_shape_vtx);
    chitra_arena_term(&s_text_vtx);
    chitra_arena_term(&s_index);
    chitra_arena_term(&s_cmds);
    
    if (s_batches) { free(s_batches); s_batches = NULL; }
    s_batch_count = 0;
}

void chitra_cmd_clear(void) {
    chitra_arena_clear(&s_shape_vtx);
    chitra_arena_clear(&s_text_vtx);
    chitra_arena_clear(&s_index);
    chitra_arena_clear(&s_cmds);
    
    /* Reset clip table: keep slot 0 as the default full-screen clip */
    s_clip_table[0] = (Chitra_ClipRect){0, 0, 16384, 16384};
    s_clip_table_count = 1;
    s_clip_stack[0] = 0;
    s_clip_stack_depth = 1;
    
    s_cmd_count = 0;
    s_shape_vertex_count = 0;
    s_text_vertex_count = 0;
    s_dirty = true;
}

/* =========================================================================
 * Clip Stack
 * ========================================================================= */

uint16_t chitra_cmd_push_clip(int x, int y, int w, int h) {
    if (s_clip_table_count >= CHITRA_MAX_CLIP_DEPTH) {
        printf("[Chitra] WARNING: Clip table full.\n");
        return s_clip_stack[s_clip_stack_depth - 1];
    }
    
    /* Intersect with the current active clip */
    uint16_t parent_id = s_clip_stack[s_clip_stack_depth - 1];
    Chitra_ClipRect parent = s_clip_table[parent_id];
    
    int x1 = x > parent.x ? x : parent.x;
    int y1 = y > parent.y ? y : parent.y;
    int x2_new = x + w;
    int x2_par = parent.x + parent.w;
    int y2_new = y + h;
    int y2_par = parent.y + parent.h;
    int x2 = x2_new < x2_par ? x2_new : x2_par;
    int y2 = y2_new < y2_par ? y2_new : y2_par;
    
    uint16_t id = (uint16_t)s_clip_table_count;
    s_clip_table[id] = (Chitra_ClipRect){
        x1, y1,
        x2 > x1 ? x2 - x1 : 0,
        y2 > y1 ? y2 - y1 : 0
    };
    s_clip_table_count++;
    
    if (s_clip_stack_depth < CHITRA_MAX_CLIP_DEPTH) {
        s_clip_stack[s_clip_stack_depth++] = id;
    }
    
    return id;
}

void chitra_cmd_pop_clip(void) {
    if (s_clip_stack_depth > 1) {
        s_clip_stack_depth--;
    }
}

const Chitra_ClipRect* chitra_cmd_get_clip(uint16_t clip_id) {
    if (clip_id < s_clip_table_count) {
        return &s_clip_table[clip_id];
    }
    return &s_clip_table[0];
}

/* =========================================================================
 * Push Helpers (write vertices + indices + commands directly into arenas)
 * ========================================================================= */

void chitra_cmd_push_rect(int z_index, float x, float y, float w, float h,
                          float radius, float border,
                          uint32_t fill_color, uint32_t border_color) {
    /* Write 4 vertices directly into the shape vertex arena */
    uint32_t vtx_offset = (uint32_t)s_shape_vtx.size;
    
    Chitra_ShapeVertex quad[4] = {
        { {x,     y    }, {0.0f, 0.0f}, {w, h}, {radius, border}, fill_color, border_color },
        { {x + w, y    }, {1.0f, 0.0f}, {w, h}, {radius, border}, fill_color, border_color },
        { {x + w, y + h}, {1.0f, 1.0f}, {w, h}, {radius, border}, fill_color, border_color },
        { {x,     y + h}, {0.0f, 1.0f}, {w, h}, {radius, border}, fill_color, border_color }
    };
    chitra_arena_push(&s_shape_vtx, sizeof(quad));
    memcpy(s_shape_vtx.base + vtx_offset, quad, sizeof(quad));
    
    /* Write 6 indices */
    uint32_t idx_offset = (uint32_t)s_index.size;
    uint16_t base = s_shape_vertex_count;
    uint16_t indices[6] = {
        (uint16_t)(base + 0), (uint16_t)(base + 1), (uint16_t)(base + 2),
        (uint16_t)(base + 0), (uint16_t)(base + 2), (uint16_t)(base + 3)
    };
    chitra_arena_push(&s_index, sizeof(indices));
    memcpy(s_index.base + idx_offset, indices, sizeof(indices));
    
    s_shape_vertex_count += 4;
    
    /* Write 1 command */
    uint16_t clip_id = s_clip_stack[s_clip_stack_depth - 1];
    Chitra_DrawCmd cmd = {
        .pipeline = CHITRA_PIPELINE_SHAPE,
        .clip_id = clip_id,
        .texture_id = 0,
        .vertex_offset = vtx_offset,
        .index_offset = idx_offset,
        .elem_count = 6
    };
    
    /* Store the z_index in a parallel array via sort keys (below).
     * For now, pack it with the command. We use a temporary struct. */
    /* Actually, we store z_index separately in the sort key generation. 
     * Let's add a small wrapper. */
    
    /* We need z_index for sort key generation. Store it in front of the command. */
    typedef struct {
        int z_index;
        Chitra_DrawCmd cmd;
    } InternalCmd;
    
    InternalCmd icmd = { .z_index = z_index, .cmd = cmd };
    chitra_arena_push(&s_cmds, sizeof(InternalCmd));
    memcpy(s_cmds.base + s_cmds.size - sizeof(InternalCmd), &icmd, sizeof(InternalCmd));
    
    s_cmd_count++;
    s_dirty = true;
}

void chitra_cmd_push_text(int z_index, uint16_t texture_id,
                          const Chitra_TextVertex* vertices, int quad_count) {
    if (quad_count <= 0 || !vertices) return;
    
    int vert_count = quad_count * 4;
    
    /* Write vertices */
    uint32_t vtx_offset = (uint32_t)s_text_vtx.size;
    size_t vtx_size = vert_count * sizeof(Chitra_TextVertex);
    chitra_arena_push(&s_text_vtx, vtx_size);
    memcpy(s_text_vtx.base + vtx_offset, vertices, vtx_size);
    
    /* Write indices */
    uint32_t idx_offset = (uint32_t)s_index.size;
    int idx_count = quad_count * 6;
    size_t idx_size = idx_count * sizeof(uint16_t);
    uint16_t* idx_ptr = (uint16_t*)chitra_arena_push(&s_index, idx_size);
    
    for (int q = 0; q < quad_count; q++) {
        uint16_t base = s_text_vertex_count + (uint16_t)(q * 4);
        int i = q * 6;
        idx_ptr[i + 0] = (uint16_t)(base + 0);
        idx_ptr[i + 1] = (uint16_t)(base + 1);
        idx_ptr[i + 2] = (uint16_t)(base + 2);
        idx_ptr[i + 3] = (uint16_t)(base + 0);
        idx_ptr[i + 4] = (uint16_t)(base + 2);
        idx_ptr[i + 5] = (uint16_t)(base + 3);
    }
    
    s_text_vertex_count += (uint16_t)vert_count;
    
    /* Write command */
    uint16_t clip_id = s_clip_stack[s_clip_stack_depth - 1];
    Chitra_DrawCmd cmd = {
        .pipeline = CHITRA_PIPELINE_TEXTURE,
        .clip_id = clip_id,
        .texture_id = texture_id,
        .vertex_offset = vtx_offset,
        .index_offset = idx_offset,
        .elem_count = (uint32_t)idx_count
    };
    
    typedef struct {
        int z_index;
        Chitra_DrawCmd cmd;
    } InternalCmd;
    
    InternalCmd icmd = { .z_index = z_index, .cmd = cmd };
    chitra_arena_push(&s_cmds, sizeof(InternalCmd));
    memcpy(s_cmds.base + s_cmds.size - sizeof(InternalCmd), &icmd, sizeof(InternalCmd));
    
    s_cmd_count++;
    s_dirty = true;
}

/* =========================================================================
 * Dirty Flag
 * ========================================================================= */

bool chitra_cmd_is_dirty(void) {
    return s_dirty;
}

void chitra_cmd_set_dirty(void) {
    s_dirty = true;
}

/* =========================================================================
 * Retained-Mode Accessors
 * ========================================================================= */

const void* chitra_cmd_get_shape_vertices(size_t* out_size) {
    if (out_size) *out_size = s_shape_vtx.size;
    return s_shape_vtx.base;
}

const void* chitra_cmd_get_text_vertices(size_t* out_size) {
    if (out_size) *out_size = s_text_vtx.size;
    return s_text_vtx.base;
}

const void* chitra_cmd_get_indices(size_t* out_size) {
    if (out_size) *out_size = s_index.size;
    return s_index.base;
}

const Chitra_DrawBatch* chitra_cmd_get_batches(int* out_count) {
    if (out_count) *out_count = s_batch_count;
    return s_batches;
}

/* =========================================================================
 * 64-Bit Sort Key Generation, Sorting, and Batch Merging
 * ========================================================================= */

typedef struct {
    int z_index;
    Chitra_DrawCmd cmd;
} InternalCmd;

static int compare_sort_keys(const void* a, const void* b) {
    uint64_t ka = *(const uint64_t*)a;
    uint64_t kb = *(const uint64_t*)b;
    if (ka < kb) return -1;
    if (ka > kb) return 1;
    return 0;
}

void chitra_cmd_prepare(void) {
    if (!s_dirty) {
        return;
    }
    
    if (s_cmd_count == 0) {
        s_batch_count = 0;
        s_shape_vertex_count = 0;
        s_text_vertex_count = 0;
        s_dirty = false;
        return;
    }
    
    InternalCmd* cmds = (InternalCmd*)s_cmds.base;
    
    /* --- Step 1: Generate 64-bit sort keys --- */
    uint64_t* sort_keys = (uint64_t*)malloc(s_cmd_count * sizeof(uint64_t));
    
    for (int i = 0; i < s_cmd_count; i++) {
        uint64_t z      = (uint64_t)((uint16_t)(cmds[i].z_index & 0xFFFF));
        uint64_t clip   = (uint64_t)cmds[i].cmd.clip_id;
        uint64_t pipe   = (uint64_t)cmds[i].cmd.pipeline;
        uint64_t tex    = (uint64_t)cmds[i].cmd.texture_id;
        uint64_t idx    = (uint64_t)(i & 0xFFFF);
        
        sort_keys[i] = (z    << 48)
                      | (clip << 32)
                      | (pipe << 24)
                      | (tex  << 16)
                      | idx;
    }
    
    /* --- Step 2: Sort the 64-bit keys --- */
    qsort(sort_keys, s_cmd_count, sizeof(uint64_t), compare_sort_keys);
    
    /* --- Step 3: Walk sorted keys and merge compatible batches --- */
    if (s_batches) { free(s_batches); s_batches = NULL; }
    /* Worst case: every command is its own batch */
    s_batches = (Chitra_DrawBatch*)malloc(s_cmd_count * sizeof(Chitra_DrawBatch));
    s_batch_count = 0;
    
    for (int i = 0; i < s_cmd_count; i++) {
        uint16_t cmd_idx = (uint16_t)(sort_keys[i] & 0xFFFF);
        InternalCmd* ic = &cmds[cmd_idx];
        Chitra_DrawCmd* dc = &ic->cmd;
        
        /* Convert byte offset to element offset for index buffer */
        uint32_t base_elem = dc->index_offset / sizeof(uint16_t);
        
        /* Try to merge with the previous batch */
        if (s_batch_count > 0) {
            Chitra_DrawBatch* prev = &s_batches[s_batch_count - 1];
            if (prev->pipeline == dc->pipeline &&
                prev->clip_id == dc->clip_id &&
                prev->texture_id == dc->texture_id &&
                (prev->base_element + prev->num_elements) == base_elem) {
                /* Merge: extend the previous batch */
                prev->num_elements += dc->elem_count;
                continue;
            }
        }
        
        /* Start a new batch */
        s_batches[s_batch_count++] = (Chitra_DrawBatch){
            .pipeline = dc->pipeline,
            .clip_id = dc->clip_id,
            .texture_id = dc->texture_id,
            .base_element = base_elem,
            .num_elements = dc->elem_count
        };
    }
    
    free(sort_keys);
    
    /* --- Step 4: Upload to GPU --- */
    const Chitra_RHI_API* rhi = chitra_rhi_get_api();
    if (rhi && rhi->upload_vertices) {
        rhi->upload_vertices(
            s_shape_vtx.base, s_shape_vtx.size,
            s_text_vtx.base, s_text_vtx.size,
            (const uint16_t*)s_index.base, s_index.size
        );
    }
    
    s_dirty = false;
    
    /* Reset vertex counters for next frame (offsets into index buffer) */
    s_shape_vertex_count = 0;
    s_text_vertex_count = 0;
}

void chitra_cmd_draw(void) {
    if (s_batch_count == 0) return;
    
    const Chitra_RHI_API* rhi = chitra_rhi_get_api();
    if (!rhi) return;
    
    for (int i = 0; i < s_batch_count; i++) {
        Chitra_DrawBatch* b = &s_batches[i];
        
        /* Apply scissor for this batch */
        const Chitra_ClipRect* clip = chitra_cmd_get_clip(b->clip_id);
        if (rhi->set_scissor) {
            rhi->set_scissor(clip->x, clip->y, clip->w, clip->h);
        }
        
        if (rhi->draw_batch) {
            rhi->draw_batch(b);
        }
    }
}
