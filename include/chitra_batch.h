#ifndef CHITRA_BATCH_H
#define CHITRA_BATCH_H

#include "chitra_table.h"
#include <stdint.h>
#include <stdbool.h>

/* =========================================================================
 * 16-Byte Sorter Struct (4 entries per 64-byte L1 Cache Line)
 * ========================================================================= */

typedef struct {
    uint64_t sort_key;   /* 8 bytes: Visual state for Radix Sort */
    uint32_t command_id; /* 4 bytes: Slot index in CommandTable */
    uint32_t padding;    /* 4 bytes: Preserves exact 16-byte alignment */
} Chitra_SortEntry;

/* =========================================================================
 * Draw Batch Specification
 * ========================================================================= */

typedef struct {
    uint8_t  pipeline;     /* Target shader pipeline (Shape, Text, etc.) */
    uint16_t clip_id;      /* Scissor clip region index */
    uint16_t texture_id;   /* Texture atlas or image descriptor */
    uint32_t base_element; /* Starting index in 32-bit index buffer */
    uint32_t num_elements; /* Total indices to draw */
} Chitra_DrawBatch;

/* =========================================================================
 * Batching & Sorting Subsystem Lifecycle
 * ========================================================================= */

void chitra_batch_init(void);
void chitra_batch_term(void);

/*
 * Executes the frame sorting and batching pipeline:
 * 1. Collects active elements (vertex_count > 0) from CommandTable
 * 2. Runs 8-pass LSD Radix Sort on 16-byte SortEntry structs
 * 3. Streams 32-bit indices with zero-cost dead hole skipping
 * 4. Merges compatible layers via 34-bit state mask coalescing
 */
void chitra_batch_prepare(void);

/* Accessors for GPU upload and RHI dispatch */
const uint32_t*         chitra_batch_get_indices(uint32_t* out_count);
const Chitra_DrawBatch* chitra_batch_get_batches(uint32_t* out_count);

#endif /* CHITRA_BATCH_H */
