#include "chitra_batch.h"
#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * Internal Dynamic Buffers
 * ========================================================================= */

static Chitra_SortEntry* s_sort_entries     = NULL;
static Chitra_SortEntry* s_pingpong_entries = NULL;
static uint32_t          s_sort_capacity    = 0;
static uint32_t          s_active_count     = 0;

static uint32_t*         s_indices          = NULL;
static uint32_t          s_index_capacity   = 0;
static uint32_t          s_index_count      = 0;

static Chitra_DrawBatch* s_batches          = NULL;
static uint32_t          s_batch_capacity   = 0;
static uint32_t          s_batch_count      = 0;

#define CHITRA_INIT_SORT_CAPACITY   64
#define CHITRA_INIT_INDEX_CAPACITY  384 /* 64 quads * 6 indices */
#define CHITRA_INIT_BATCH_CAPACITY  16

/* =========================================================================
 * Subsystem Lifecycle
 * ========================================================================= */

void chitra_batch_init(void) {
    s_sort_capacity = CHITRA_INIT_SORT_CAPACITY;
    s_sort_entries = (Chitra_SortEntry*)malloc(s_sort_capacity * sizeof(Chitra_SortEntry));
    s_pingpong_entries = (Chitra_SortEntry*)malloc(s_sort_capacity * sizeof(Chitra_SortEntry));
    s_active_count = 0;

    s_index_capacity = CHITRA_INIT_INDEX_CAPACITY;
    s_indices = (uint32_t*)malloc(s_index_capacity * sizeof(uint32_t));
    s_index_count = 0;

    s_batch_capacity = CHITRA_INIT_BATCH_CAPACITY;
    s_batches = (Chitra_DrawBatch*)malloc(s_batch_capacity * sizeof(Chitra_DrawBatch));
    s_batch_count = 0;
}

void chitra_batch_term(void) {
    if (s_sort_entries)     { free(s_sort_entries);     s_sort_entries = NULL; }
    if (s_pingpong_entries) { free(s_pingpong_entries); s_pingpong_entries = NULL; }
    s_sort_capacity = 0;
    s_active_count = 0;

    if (s_indices) { free(s_indices); s_indices = NULL; }
    s_index_capacity = 0;
    s_index_count = 0;

    if (s_batches) { free(s_batches); s_batches = NULL; }
    s_batch_capacity = 0;
    s_batch_count = 0;
}

/* =========================================================================
 * Internal Dynamic Growth
 * ========================================================================= */

static void ensure_sort_capacity(uint32_t count) {
    if (count > s_sort_capacity) {
        uint32_t new_cap = s_sort_capacity + (s_sort_capacity >> 1);
        if (new_cap < count) new_cap = count + 64;

        s_sort_entries = (Chitra_SortEntry*)realloc(s_sort_entries, new_cap * sizeof(Chitra_SortEntry));
        s_pingpong_entries = (Chitra_SortEntry*)realloc(s_pingpong_entries, new_cap * sizeof(Chitra_SortEntry));
        s_sort_capacity = new_cap;
    }
}

static void ensure_index_capacity(uint32_t count) {
    if (count > s_index_capacity) {
        uint32_t new_cap = s_index_capacity + (s_index_capacity >> 1);
        if (new_cap < count) new_cap = count + 256;

        s_indices = (uint32_t*)realloc(s_indices, new_cap * sizeof(uint32_t));
        s_index_capacity = new_cap;
    }
}

static void ensure_batch_capacity(uint32_t count) {
    if (count > s_batch_capacity) {
        uint32_t new_cap = s_batch_capacity + (s_batch_capacity >> 1);
        if (new_cap < count) new_cap = count + 16;

        s_batches = (Chitra_DrawBatch*)realloc(s_batches, new_cap * sizeof(Chitra_DrawBatch));
        s_batch_capacity = new_cap;
    }
}

/* =========================================================================
 * 8-Pass LSD Radix Sort (16-Byte Aligned Structs)
 * ========================================================================= */

static void lsd_radix_sort(Chitra_SortEntry* src, Chitra_SortEntry* tmp, uint32_t n) {
    if (n <= 1) return;

    Chitra_SortEntry* current_src = src;
    Chitra_SortEntry* current_dst = tmp;

    /* 8 passes for 64-bit integer keys (8 bits per pass) */
    for (int pass = 0; pass < 8; pass++) {
        uint32_t shift = pass * 8;
        uint32_t counts[256] = {0};
        uint32_t offsets[256] = {0};

        /* 1. Build histogram */
        for (uint32_t i = 0; i < n; i++) {
            uint8_t bucket = (uint8_t)((current_src[i].sort_key >> shift) & 0xFF);
            counts[bucket]++;
        }

        /* 2. Compute prefix sum offsets */
        offsets[0] = 0;
        for (int b = 1; b < 256; b++) {
            offsets[b] = offsets[b - 1] + counts[b - 1];
        }

        /* 3. Distribute entries into destination buffer */
        for (uint32_t i = 0; i < n; i++) {
            uint8_t bucket = (uint8_t)((current_src[i].sort_key >> shift) & 0xFF);
            current_dst[offsets[bucket]++] = current_src[i];
        }

        /* 4. Swap ping-pong pointers for next pass */
        Chitra_SortEntry* swap = current_src;
        current_src = current_dst;
        current_dst = swap;
    }

    /* Because 8 passes is even, sorted data ends up back in original src array */
}

/* =========================================================================
 * Batch Preparation Pipeline
 * ========================================================================= */

void chitra_batch_prepare(void) {
    const Chitra_CommandTable* cmd_table = chitra_table_get_commands();
    if (!cmd_table || cmd_table->count == 0) {
        s_active_count = 0;
        s_index_count = 0;
        s_batch_count = 0;
        return;
    }

    /* Step 1: Collect active elements (vertex_count > 0) */
    s_active_count = 0;
    ensure_sort_capacity(cmd_table->count);

    for (uint32_t i = 0; i < cmd_table->count; i++) {
        if (chitra_slot_is_alive(&cmd_table->slots[i]) && chitra_slot_vertex_count(&cmd_table->slots[i]) > 0) {
            s_sort_entries[s_active_count].sort_key   = cmd_table->slots[i].sort_key;
            s_sort_entries[s_active_count].command_id = i;
            s_sort_entries[s_active_count].padding    = 0;
            s_active_count++;
        }
    }

    if (s_active_count == 0) {
        s_index_count = 0;
        s_batch_count = 0;
        return;
    }

    /* Step 2: 8-Pass LSD Radix Sort across contiguous 16-byte structs */
    lsd_radix_sort(s_sort_entries, s_pingpong_entries, s_active_count);

    /* Step 3: Stream 32-bit Index Buffer & Coalesce State-Aware Batches */
    s_index_count = 0;
    s_batch_count = 0;

    uint64_t prev_state = ~0ULL;
    uint8_t  prev_pipe  = 0xFF;

    for (uint32_t i = 0; i < s_active_count; i++) {
        uint32_t cmd_id = s_sort_entries[i].command_id;
        const Chitra_CommandSlot* slot = &cmd_table->slots[cmd_id];

        uint64_t full_key   = s_sort_entries[i].sort_key;
        uint64_t state      = full_key & CHITRA_STATE_MASK;
        uint8_t  pipeline   = (uint8_t)((full_key >> 13) & CHITRA_PIPELINE_MASK);
        uint16_t clip_id    = (uint16_t)((full_key >> 18) & CHITRA_CLIP_ID_MASK);
        uint16_t texture_id = (uint16_t)(full_key & CHITRA_TEXTURE_ID_MASK);

        uint32_t quad_count = chitra_slot_vertex_count(slot) / 4;
        uint32_t indices_for_slot = quad_count * 6;

        /* Check if current quad state matches previous batch state */
        bool can_coalesce = (state == prev_state && pipeline == prev_pipe && s_batch_count > 0);

        if (!can_coalesce) {
            /* Start a new draw batch */
            ensure_batch_capacity(s_batch_count + 1);
            Chitra_DrawBatch* batch = &s_batches[s_batch_count++];
            batch->pipeline     = pipeline;
            batch->clip_id      = clip_id;
            batch->texture_id   = texture_id;
            batch->base_element = s_index_count;
            batch->num_elements = 0;

            prev_state = state;
            prev_pipe  = pipeline;
        }

        /* Emit 6 indices per 4-vertex quad (Skipping dead memory holes in staging array) */
        ensure_index_capacity(s_index_count + indices_for_slot);

        for (uint32_t q = 0; q < quad_count; q++) {
            uint32_t base = slot->start_vertex_index + (q * 4);

            s_indices[s_index_count + 0] = base + 0;
            s_indices[s_index_count + 1] = base + 1;
            s_indices[s_index_count + 2] = base + 2;
            s_indices[s_index_count + 3] = base + 0;
            s_indices[s_index_count + 4] = base + 2;
            s_indices[s_index_count + 5] = base + 3;

            s_index_count += 6;
        }

        /* Accumulate indices into active batch */
        s_batches[s_batch_count - 1].num_elements += indices_for_slot;
    }
}

/* =========================================================================
 * Accessors
 * ========================================================================= */

const uint32_t* chitra_batch_get_indices(uint32_t* out_count) {
    if (out_count) *out_count = s_index_count;
    return s_indices;
}

const Chitra_DrawBatch* chitra_batch_get_batches(uint32_t* out_count) {
    if (out_count) *out_count = s_batch_count;
    return s_batches;
}
