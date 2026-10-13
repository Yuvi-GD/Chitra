# Chapter 02: Memory Model and Data Structures

## 1. Overview of the Memory Architecture

Chitra avoids the memory overhead and pointer indirection of traditional rendering engines by maintaining all geometry and command state in contiguous, data-oriented arrays in system RAM.

The memory model is governed by three foundational design requirements:

1. **Zero Hardcoded Limits:** Memory begins with minimal allocations and expands dynamically on demand.
2. **CPU Cache Coherency:** Critical structures are packed to cache-line boundaries (64 bytes) to maximize L1/L2 data cache throughput.
3. **Hardware Stride Matching:** Vertex buffers in system RAM match GPU vertex layouts 1:1, eliminating runtime data translation or vertex copying.

```
+------------------------------------------------------------------------+
|                        CPU SYSTEM RAM (Chitra)                         |
+------------------------------------------------------------------------+
| 1. Dynamic CommandTable (Slot Map)                                     |
|    - Contiguous array of 16-byte Chitra_CommandSlot structs            |
|    - Grows dynamically by 1.5x on capacity exhaustion                  |
+------------------------------------------------------------------------+
| 2. Dynamic free_list (ID Recycler Stack)                               |
|    - Contiguous array of recycled 32-bit slot indices                  |
|    - LIFO ordering keeps active slot IDs densely packed near index 0   |
+------------------------------------------------------------------------+
| 3. Per-Pipeline Staging Arrays (staging_arrays[MAX_PIPELINES])         |
|    - Pipeline 0 (Shape): Contiguous Chitra_ShapeVertex (40 bytes)      |
|    - Pipeline 1 (Text):  Contiguous Chitra_TextVertex  (20 bytes)      |
|    - Pipeline 2 (Mesh):  Contiguous Chitra_TextVertex  (20 bytes)      |
|    - Grows dynamically by 1.75x upon compaction / reallocation         |
+-----------------------------------+------------------------------------+
                                    | (Uploaded to GPU via Sokol GFX)
+-----------------------------------v------------------------------------+
|                           GPU VRAM (Sokol)                             |
+------------------------------------------------------------------------+
| - GPU Vertex Buffers: Streamed from staging_arrays                      |
| - Unified 32-Bit Index Buffer: Hole-skipping index stream              |
+------------------------------------------------------------------------+
```

---

## 2. The Dynamic 2-Table Model

Chitra eliminates parallel tracking lists, active/inactive bitsets, and dirty arrays. All element metadata is tracked using **exactly two tables**: the `CommandTable` and the `free_list`.

```c
typedef struct {
    uint64_t sort_key;           // 8 bytes: Pre-compiled 64-bit Radix Sort key
    uint32_t start_vertex_index; // 4 bytes: Pipeline-local vertex offset in staging array
    uint32_t vertex_meta;        // 4 bytes: [Bit 31: Alive Flag | Bits 0..30: Vertex Count]
} Chitra_CommandSlot; // Exactly 16 bytes! Four slots fit in a 64-byte cache line.

typedef struct {
    Chitra_CommandSlot* slots;    // Dynamically allocated array of slots
    uint32_t            count;    // High-water mark of allocated slots
    uint32_t            capacity; // Current allocated slot capacity
} Chitra_CommandTable;

typedef struct {
    uint32_t* ids;      // Dynamically allocated array of recycled slot indices
    uint32_t  count;    // Current number of available IDs in the stack
    uint32_t  capacity; // Current allocated capacity of the free-list
} Chitra_FreeList;

Chitra_CommandTable command_table;
Chitra_FreeList     free_list;
```

### The 16-Byte Command Slot Struct (Address Book in RAM)
Every active or inactive element in Chitra is represented in RAM by a single `Chitra_CommandSlot`:
* **`sort_key` (8 bytes):** Stores the fully assembled 64-bit integer Radix Sort key representing pure visual GPU state (30-bit Draw Order, 16-bit Clip ID, 5-bit Pipeline ID, 13-bit Texture ID). It is computed once when the element is created or when its layer/clip changes. The render loop copies this key directly without performing bitwise math on every frame.
* **`start_vertex_index` (4 bytes):** The pipeline-local vertex index where the element's geometry begins in its respective staging buffer.
* **`vertex_meta` (4 bytes):** Combined 32-bit field where Bit 31 represents the **Alive Flag** (`0x80000000U`) and Bits 0..30 represent the **Vertex Count** (supporting up to 2,147,483,647 vertices per element).
  * **Active vs. Dead State:** If bit 31 is set (`chitra_slot_is_alive(slot)`), the slot is **alive**. When destroyed or empty (`vertex_meta == 0`), the slot is **dead / recycled**. Helper accessors `chitra_slot_is_alive()` and `chitra_slot_vertex_count()` provide zero-cost bitfield extraction.

### The 16-Byte Sorter Struct: Chitra_SortEntry
During the active frame sorting pass, Chitra decouples the `command_id` from the 64-bit `sort_key`. This grants the sort key 30 full bits for Draw Order (~1.07 billion layers, perfectly matching the 32-bit Index Buffer capacity) instead of constraining it to 16 bits (65,535 layers).

To map sorted keys back to their slots in `CommandTable` with zero pointer lookups, the render pass populates flat contiguous arrays of `Chitra_SortEntry`:

```c
typedef struct {
    uint64_t sort_key;   // 8 bytes: Visual state for Radix Sort
    uint32_t command_id; // 4 bytes: Index pointing directly back to CommandTable
    uint32_t padding;    // 4 bytes: Preserves exact 16-byte cache alignment
} Chitra_SortEntry;      // Exactly 16 bytes! Four entries fit in a 64-byte cache line.
```

### Cache-Line Alignment (16 Bytes)
Standard x86-64 and ARM64 CPU architectures utilize 64-byte L1 data cache lines. 
Because both `sizeof(Chitra_CommandSlot) == 16` and `sizeof(Chitra_SortEntry) == 16`:
* Exactly **four slots / entries** fit in a single 64-byte cache line with zero padding waste.
* During the render loop, scanning 256 UI elements requires reading only 4 KB of memory (64 cache lines), residing entirely in L1 data cache.

### Growth Multiplier: 1.5x for Metadata Tables
When `command_table.count >= command_table.capacity` or `free_list.count >= free_list.capacity`:
* Both tables expand using a **1.5x capacity multiplier** via `realloc()`.
* **Rationale for 1.5x:** Metadata tables experience steady, predictable growth based on UI element counts. A 1.5x multiplier avoids excessive memory overallocation while amortizing the reallocation cost to $O(1)$. Initial capacity typically starts at 1,024 slots (16 KB).

### LIFO Free-List Dense Packing
The `free_list` operates as a Last-In, First-Out (LIFO) stack.
* When an element is destroyed, its ID is pushed to the top of `free_list`.
* When a new element is created, Chitra pops the most recently freed ID from `free_list`.
* **The Performance Consequence:** Reusing recently destroyed IDs prevents ID fragmentation. Slot IDs stay tightly packed near index `0`. In an application with 200 visible widgets, `command_table.count` stays near 200, allowing the render loop to scan only 3.2 KB of memory instead of scanning across millions of empty indices.

---

## 3. Per-Pipeline Staging Arrays

Rather than padding all vertex types to a uniform struct (which would waste 50% of CPU cache and GPU bandwidth on text glyphs), Chitra maintains dedicated staging buffers per shader pipeline.

```c
typedef struct {
    void*    data;        // Contiguous vertex buffer in system RAM
    uint32_t cursor;      // Total physical vertices written (append position)
    uint32_t capacity;    // Maximum vertex capacity before reallocation
    uint32_t dead_count;  // Total number of abandoned/dead vertices
} Chitra_PipelineData;

Chitra_PipelineData staging_arrays[MAX_PIPELINES];
```

* **`cursor`:** Represents the high-water mark of vertices written into the buffer. New or grown elements append at `cursor`.
* **`dead_count`:** Tracks vertices that have been abandoned due to text growth, resizes, or element deletions.
* **Active Vertices:** At any given time, active geometry is calculated as:
  $$\text{Active Vertices} = \text{cursor} - \text{dead\_count}$$

### Pipeline-Local Vertex Indexing
Vertex indices stored in `CommandTable.start_vertex_index` are **pipeline-local integers**, not global byte offsets.
* For the **Shape Pipeline**, index `0` refers to the first 40-byte `Chitra_ShapeVertex` in `staging_arrays[PIPELINE_SHAPE].data`. Index `4` refers to the second quad.
* For the **Text Pipeline**, index `0` refers to the first 20-byte `Chitra_TextVertex` in `staging_arrays[PIPELINE_TEXT].data`.
* When Sokol GFX binds a pipeline, it binds that pipeline's corresponding vertex buffer (`vbuf[pipeline]`). The 32-bit Index Buffer references indices local to the currently bound buffer.

### Growth Multiplier: 1.75x for Staging Arrays
When a staging buffer runs out of capacity:
* It expands using a **1.75x capacity multiplier** during compaction.
* **Rationale for 1.75x:** Unlike metadata tables, vertex staging buffers suffer from internal fragmentation due to text appending and dead holes. A higher 1.75x multiplier provides necessary headroom so that animated text blocks and dynamic layouts do not trigger frequent reallocations.

---

## 4. Vertex Structures and Memory Layouts

Chitra defines two strictly packed vertex structures matching hardware layout specifications:

### A. The Shape Vertex (40 Bytes)
Used for rectangles, rounded panels, pill buttons, borders, and modal containers. Four vertices define a single quad (two triangles).

```c
typedef struct {
    float    pos[2];           // 8 bytes: X, Y screen coordinates in pixels
    float    uv[2];            // 8 bytes: Normalized box coordinates (0.0 to 1.0)
    float    size[2];          // 8 bytes: Physical width and height in pixels
    float    radius_border[2]; // 8 bytes: Corner radius, border thickness in pixels
    uint32_t fill_color;       // 4 bytes: Packed RGBA8 (UBYTE4N)
    uint32_t border_color;     // 4 bytes: Packed RGBA8 (UBYTE4N)
} Chitra_ShapeVertex; // Exactly 40 bytes
```

#### Memory Layout Diagram (40 Bytes)
```
Offset  0: [ pos.x (4B)       ][ pos.y (4B)       ] -> 8 bytes
Offset  8: [ uv.u (4B)        ][ uv.v (4B)        ] -> 8 bytes
Offset 16: [ size.w (4B)      ][ size.h (4B)      ] -> 8 bytes
Offset 24: [ radius (4B)      ][ border (4B)      ] -> 8 bytes
Offset 32: [ fill_color (4B)  ]                     -> 4 bytes
Offset 36: [ border_color (4B)]                     -> 4 bytes
Total: 40 bytes per vertex (160 bytes per quad)
```

### B. The Text & Texture Vertex (20 Bytes)
Used for font glyphs sampled from the alpha atlas, UI texture images, and triangulated vector meshes. Four vertices define a single glyph or quad.

```c
typedef struct {
    float    pos[2];           // 8 bytes: X, Y screen coordinates in pixels
    float    uv[2];            // 8 bytes: Normalized texture/atlas UV coordinates
    uint32_t color;            // 4 bytes: Packed RGBA8 tint (UBYTE4N)
} Chitra_TextVertex; // Exactly 20 bytes
```

#### Memory Layout Diagram (20 Bytes)
```
Offset  0: [ pos.x (4B) ][ pos.y (4B) ] -> 8 bytes
Offset  8: [ uv.u (4B)  ][ uv.v (4B)  ] -> 8 bytes
Offset 16: [ color (4B) ]               -> 4 bytes
Total: 20 bytes per vertex (80 bytes per quad)
```

---

## 5. The Memory Hole Concept and Hole-Skipping

In traditional engines, deleting an element or changing text length requires shifting all subsequent vertices forward via `memmove()`. On a UI with thousands of vertices, doing this every time a label updates causes severe CPU stalls and invalidates cached vertex indices.

Chitra eliminates memory moves through **Dead Memory Holes** and **32-Bit Index Buffer Hole-Skipping**:

```
Staging Array in System RAM:
┌─────────────────┬───────────────────┬─────────────────┬─────────────────┐
│ Quad 0 (Alive)  │ Quad 1 (DEAD HOLE)│ Quad 2 (Alive)  │ Quad 3 (Alive)  │
│ Vertices 0..3   │ Vertices 4..7     │ Vertices 8..11  │ Vertices 12..15 │
└────────┬────────┴───────────────────┴────────┬────────┴────────┬────────┘
         │                                     │                 │
         │ (Only alive quads emit indices)     │                 │
         ▼                                     ▼                 ▼
GPU 32-Bit Index Buffer:
[ 0, 1, 2, 0, 2, 3,  8, 9, 10, 8, 10, 11,  12, 13, 14, 12, 14, 15 ]
 (Dead vertices 4..7 are completely skipped by the index stream!)
```

### Dynamic Text: In-Place Shrink vs. Append Growth
When a text label changes content:

1. **Text Shrink (In-Place Memory Preservation):**
   * If a label changes from `"Processing request..."` (21 characters) to `"Done"` (4 characters):
   * Chitra overwrites the first 4 glyph quads directly into the existing staging slot.
   * `CommandTable.slots[id].vertex_count` is updated from 84 to 16 vertices.
   * The remaining 68 vertices at the tail of the block are abandoned:
     $$\text{staging\_arrays}[\text{pipe}].\text{dead\_count} += 68$$
   * **Result:** No new memory is allocated. Zero array shifting occurs.

2. **Text Growth (Append with Dead Hole):**
   * If a label grows from 4 characters to 50 characters, it cannot expand in place without overwriting adjacent geometry.
   * Chitra abandons the old 4 quads:
     $$\text{staging\_arrays}[\text{pipe}].\text{dead\_count} += 16$$
   * The new 50 quads are appended at `cursor` at the end of the staging buffer:
     $$\text{start\_vertex\_index} = \text{cursor}$$
     $$\text{cursor} += 200$$
   * `CommandTable.slots[id]` is repointed to the new offset.
   * The old 16 vertices become a dead hole in RAM and VRAM.

### Why Dead Holes Cost Zero GPU Work
During the render pass, Chitra only generates sort keys for active elements (`vertex_count > 0`). When emitting indices into the GPU Index Buffer:
* The index generator reads `start_vertex_index` for alive elements only.
* The indices naturally jump over dead holes.
* The GPU rasterizer only renders triangles specified by the index buffer; it never reads, transforms, or shades the dead vertices.

---

## 6. Compaction and Memory Garbage Collection

To prevent dead holes from accumulating indefinitely, Chitra utilizes a two-tier compaction system:

```
                      Memory Threshold Evaluation
                                 |
            +--------------------+--------------------+
            |                                         |
            v                                         v
  Hard Limit Trigger                        Soft Limit Trigger
  cursor + new_alloc > capacity             dead_count > 50% capacity
            |                                         |
            v                                         v
  Immediate Compaction                      Amortized Compaction
  & 1.75x Array Growth                      (Idle frames only)
```

### Threshold 1: Hard Limit (Immediate OOM Protection)
* **Condition:** `cursor + new_allocations > capacity`
* **Trigger:** An allocation would exceed available staging buffer memory.
* **Execution:** Triggers an immediate, synchronous single-pass compaction. If active vertices still exceed capacity after packing, the buffer expands with a **1.75x multiplier**.

### Threshold 2: Soft Limit (Amortized Compaction)
* **Condition:** `dead_count > (capacity * 0.50)`
* **Trigger:** Abandoned vertices exceed 50% of the buffer.
* **Execution:** Executed **only during idle frames** when the client application reports no ongoing user input, drag gestures, or layout reflows. This prevents micro-stutters during animations.

### The Single-Pass Compaction Algorithm

```c
void chitra_compact_pipeline(uint8_t pipeline_id, uint32_t additional_needed) {
    Chitra_PipelineData* pipe = &staging_arrays[pipeline_id];
    uint32_t active_vertices = pipe->cursor - pipe->dead_count;
    uint32_t target_capacity = (uint32_t)((active_vertices + additional_needed) * 1.75f);
    if (target_capacity < 1024) target_capacity = 1024;

    size_t stride = (pipeline_id == PIPELINE_SHAPE) ? sizeof(Chitra_ShapeVertex) : sizeof(Chitra_TextVertex);

    // 1. Allocate a temporary contiguous packed buffer
    uint8_t* temp_buffer = (uint8_t*)malloc(target_capacity * stride);
    uint32_t packed_cursor = 0;

    // 2. Iterate through all active slots and pack vertices sequentially
    for (uint32_t i = 0; i < command_table.count; i++) {
        Chitra_CommandSlot* slot = &command_table.slots[i];
        if (slot->vertex_count == 0) continue;

        uint8_t slot_pipe = (slot->sort_key >> 13) & 0x1F;
        if (slot_pipe != pipeline_id) continue;

        // Copy alive vertices into contiguous packed memory
        uint8_t* src = (uint8_t*)pipe->data + (slot->start_vertex_index * stride);
        uint8_t* dst = temp_buffer + (packed_cursor * stride);
        memcpy(dst, src, slot->vertex_count * stride);

        // Repoint slot to new contiguous index
        slot->start_vertex_index = packed_cursor;
        packed_cursor += slot->vertex_count;
    }

    // 3. Swap buffers and free old fragmented memory
    free(pipe->data);
    pipe->data       = temp_buffer;
    pipe->cursor     = packed_cursor;
    pipe->capacity   = target_capacity;
    pipe->dead_count = 0;

    // 4. Force full GPU buffer upload on next frame
    global_frame_dirty = true;
}
```

---

## 7. Next Steps

To see how Chitra sorts these packed memory structures and converts them into GPU draw calls, continue to:
* **[Chapter 03: Sorting and Draw Batching Pipeline](03_sorting_and_batch_pipeline.md)**: 64-bit Radix Sort keys, 32-bit index buffer generation, and the `0x00000003FFFFFFFF` state coalescing bitmask.
