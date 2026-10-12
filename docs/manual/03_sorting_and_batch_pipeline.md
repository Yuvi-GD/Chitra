# Chapter 03: Sorting and Draw Batching Pipeline

## 1. Overview of the Batching Engine

The central performance bottleneck in 2D rendering is GPU state thrashing. Constantly swapping shaders, rebinding textures, and reconfiguring hardware scissor boundaries causes GPU pipeline stalls and driver overhead.

Traditional UI engines attempt to solve this by running standard `qsort()` on arrays of draw command structures. This introduces two severe performance penalties:
1. **CPU Cache Misses:** Sorting large command structures (32 to 64 bytes each) thrashes the CPU L1/L2 data cache.
2. **Batch Fragmentation:** Sorting algorithms that do not carefully prioritize state transitions frequently split draw calls unnecessarily across visual layers.

Chitra solves this through a **64-Bit Integer Radix Sort Key**, a **32-Bit Index Buffer Hole-Skipper**, and a **State-Aware Coalescing Bitmask**. Unchanged frames do zero sorting work, while active frames order thousands of primitives in microseconds and collapse them into the minimum theoretical number of GPU draw calls.

---

## 2. The 64-Bit Data-Oriented Sort Key

To eliminate artificial visual layering ceilings while maintaining microsecond sort speeds, Chitra represents visual GPU state as a pure **64-bit unsigned integer** (`uint64_t`).

By decoupling the `Command ID` from the sort key into a dedicated 16-byte sorter struct (`Chitra_SortEntry`), the key dedicates 30 full bits to **Draw Order** (~1.07 billion layers). This matches the ~1.07 billion quad capacity of a 32-bit Index Buffer and effortlessly handles endless UI hierarchy trees and sibling stacking without wrapping or collision risks.

### Bit Layout Diagram (Bits 63 to 0)

```
Bits 63..34 (30 bits)         Bits 33..18 (16 bits)   Bits 17..13 (5 bits)   Bits 12..0 (13 bits)
+-----------------------------+-----------------------+----------------------+--------------------+
|         Draw Order          |        Clip ID        |     Pipeline ID      |     Texture ID     |
|           30 bits           |        16 bits        |        5 bits        |      13 bits       |
+-----------------------------+-----------------------+----------------------+--------------------+
```

### Bit Field Specifications

| Field | Bit Range | Width | Shift | Mask | Description |
| :--- | :---: | :---: | :---: | :---: | :--- |
| **Draw Order** | 63..34 | 30 bits | 34 | `0x3FFFFFFF` | Visual overlapping layer / tree depth (0 to 1,073,741,823) |
| **Clip ID** | 33..18 | 16 bits | 18 | `0xFFFF` | Hardware scissor table index (0 to 65,535) |
| **Pipeline ID** | 17..13 | 5 bits | 13 | `0x1F` | Shader pipeline (Shape, Text, Mesh, etc., up to 32) |
| **Texture ID** | 12..0 | 13 bits | 0 | `0x1FFF` | Atlas index or bound texture slot (0 to 8,191) |

### Key Assembly in C
The 64-bit key is packed when an element is created or when its layer/clip state changes:

```c
uint64_t chitra_pack_sort_key(uint32_t draw_order, uint16_t clip_id, 
                              uint8_t pipeline_id, uint16_t texture_id) 
{
    return (((uint64_t)draw_order  & 0x3FFFFFFFULL) << 34)
         | (((uint64_t)clip_id     & 0xFFFFULL)     << 18)
         | (((uint64_t)pipeline_id & 0x1FULL)      << 13)
         | (((uint64_t)texture_id  & 0x1FFFULL)     << 0);
}
```

### Key Invariants

#### 1. Unbounded Hierarchical Depth (1.07 Billion Draw Orders)
Because `Draw Order` occupies 30 full bits, client UI frameworks can map hierarchical depth, z-indices, and sibling insertion order directly to integers from 0 to 1.07 billion. UI developers never encounter artificial layer boundaries.

#### 2. Painter's Algorithm Enforcement
Because `Draw Order` occupies the most significant 30 bits (bits 63..34), integer sorting mathematically guarantees that elements with lower draw orders sort before elements with higher draw orders. Background elements always draw before foreground elements.

#### 3. State Grouping within Equal Draw Orders
When elements share the same `Draw Order`, the bit layout clusters commands by their heaviest GPU state costs:
* **Clip ID (bits 33..18):** Scissor changes are grouped first, preventing scissor toggling.
* **Pipeline ID (bits 17..13):** Shaders are grouped second, preventing shader program rebinding.
* **Texture ID (bits 12..0):** Textures/atlases are grouped third, preventing texture descriptor swaps.

---

## 3. The Microsecond Radix Sort Algorithm

Traditional comparison sorts (such as quicksort) require $O(N \log N)$ comparisons and suffer from CPU branch mispredictions. Sorting 1,000 items with `qsort()` typically costs 80 to 150 microseconds on modern desktop CPUs.

Chitra sorts active elements using an **unsigned 64-bit Least Significant Digit (LSD) Radix Sort** operating on flat arrays of 16-byte `Chitra_SortEntry` structures:

* **Running Time:** $O(K \cdot N)$, where $K$ is the number of radix passes (8 passes using 8-bit digits) and $N$ is the number of active elements.
* **Branchless:** Radix sorting builds histogram buckets and scatters 16-byte entries into a ping-pong scratch buffer with zero conditional branches.
* **Cache-Coherent:** Exactly 4 entries fit in a 64-byte L1 cache line, streaming sequentially through system RAM.
* **Performance:** Sorting 1,000 entries completes in **under 15 microseconds** on standard hardware.

### Key Collection and Sorting Loop

```c
// Pre-allocated scratch buffers in system RAM
Chitra_SortEntry s_sort_entries[MAX_COMMANDS];
Chitra_SortEntry s_sort_scratch[MAX_COMMANDS];

void chitra_sort_active_elements(void) {
    uint32_t active_count = 0;

    // 1. Scan CommandTable and collect pre-compiled keys paired with slot IDs
    for (uint32_t i = 0; i < command_table.count; i++) {
        if (command_table.slots[i].vertex_count > 0) {
            s_sort_entries[active_count++] = (Chitra_SortEntry){
                .sort_key   = command_table.slots[i].sort_key,
                .command_id = i,
                .padding    = 0
            };
        }
    }

    if (active_count == 0) return;

    // 2. Execute branchless 8-pass Radix Sort on the 16-byte entries by sort_key
    radix_sort_entries(s_sort_entries, s_sort_scratch, active_count);
}
```

---

## 4. The 32-Bit Index Buffer and Hole-Skipping Mechanics

In Chitra, the GPU draws geometry by consuming a dynamic **32-bit Index Buffer** (`SG_INDEXTYPE_UINT32`).

### Why 32-Bit Indices Are Mandatory
Traditional 16-bit index buffers are limited to 65,535 vertices. Because Chitra leaves dead memory holes in staging buffers when text grows or elements resize, physical vertex offsets climb past 65,535 before memory compaction triggers. 32-bit unsigned indices safely address up to **4.29 billion vertices**, guaranteeing that hole accumulation never triggers vertex buffer overflows.

### Zero-Cost Dead Hole Skipping
Because the Radix Sort only sorts keys for alive elements (`vertex_count > 0`), the index generator only emits indices for active geometry. Dead memory holes in RAM and VRAM are naturally skipped by the index stream:

```
Staging Vertex Array in System RAM:
[ Quad 0 (Alive) ] [ Quad 1 (DEAD HOLE) ] [ Quad 2 (Alive) ] [ Quad 3 (Alive) ]
  Vertices 0..3      Vertices 4..7          Vertices 8..11     Vertices 12..15

Generated 32-Bit Index Buffer:
[ 0, 1, 2, 0, 2, 3,  8, 9, 10, 8, 10, 11,  12, 13, 14, 12, 14, 15 ]
```

* Dead vertices 4..7 are completely omitted from the index buffer.
* The GPU rasterizer reads only the indices specified in the index buffer. Dead holes cost **zero GPU vertex processing, zero rasterization, and zero fillrate**.
* The CPU never moves vertices on deletion or resize, eliminating all `memmove()` overhead.

### Quad Index Emission
Every 4-vertex quad requires 6 indices forming two triangles:

```c
void emit_quad_indices(uint32_t* index_dst, uint32_t base_vertex) {
    index_dst[0] = base_vertex + 0;
    index_dst[1] = base_vertex + 1;
    index_dst[2] = base_vertex + 2;
    index_dst[3] = base_vertex + 0;
    index_dst[4] = base_vertex + 2;
    index_dst[5] = base_vertex + 3;
}
```

---

## 5. State-Aware Draw Batch Coalescing

The central breakthrough in Chitra's batching engine is the **State-Aware Coalescing Bitmask**.

### The Draw Order Batch Splitting Trap
If an engine breaks a draw call whenever `Draw Order` changes:
* A container panel at `Draw Order = 10` and a button background at `Draw Order = 20` sharing the same shader, texture, and scissor boundary would be split into two separate draw calls.
* In complex UIs with 50 visual layers, this produces 50 distinct draw calls even if every element uses the exact same Shape pipeline.

### The 34-Bit State Mask Solution
Hardware rasterizers draw triangles in the exact order specified by the index buffer. If elements on different visual layers use the exact same shader, texture, and scissor bounds, **they can be merged into a single draw call without visual error**.

Draw Order occupies the top 30 bits (bits 63..34). The bottom 34 bits (bits 33..0) represent pure GPU state:

```c
uint64_t current_gpu_state = current_entry.sort_key & 0x00000003FFFFFFFFULL;
```

#### Bitmask Breakdown
* **`& 0x00000003FFFFFFFFULL`:** Masks out the top 30-bit `Draw Order`.
* **Remaining 34 Bits:** Exactly contains `[ Clip ID (16 bits) | Pipeline ID (5 bits) | Texture ID (13 bits) ]`.

If `current_gpu_state == previous_gpu_state`:
* **The draw call is NOT flushed.**
* Indices for the new element simply accumulate into the active batch.
* The GPU rasterizer renders them in correct visual order because the index buffer emits `Draw Order = 10` indices before `Draw Order = 20` indices.

---

## 6. The Complete State Dispatch Loop

Here is the exact dispatch loop executed in `chitra_render()` that streams indices and emits the minimum number of draw calls:

```c
void chitra_render_batches(uint32_t active_count) {
    uint64_t previous_gpu_state = 0xFFFFFFFFFFFFFFFFULL; // Initialize to invalid state
    uint16_t active_clip        = 0xFFFF;
    uint8_t  active_pipeline    = 0xFF;
    uint16_t active_texture     = 0xFFFF;

    uint32_t batch_base_index   = 0;
    uint32_t batch_index_count  = 0;
    uint32_t index_write_cursor = 0;

    for (uint32_t i = 0; i < active_count; i++) {
        Chitra_SortEntry entry = s_sort_entries[i];
        uint64_t key = entry.sort_key;
        uint64_t current_gpu_state = key & 0x00000003FFFFFFFFULL;
        uint32_t cmd_id            = entry.command_id;
        uint8_t  pipeline_id       = (uint8_t)((key >> 13) & 0x1F);

        // Check if a hardware state boundary was hit
        if (current_gpu_state != previous_gpu_state) {
            // 1. Flush accumulated batch before switching hardware state
            if (batch_index_count > 0) {
                chitra_rhi_draw(batch_base_index, batch_index_count, 1);
                batch_base_index += batch_index_count;
                batch_index_count = 0;
            }

            // 2. Apply Hardware Scissor if Clip ID changed
            uint16_t new_clip = (uint16_t)((key >> 18) & 0xFFFF);
            if (new_clip != active_clip) {
                Chitra_ClipRect r = clip_table[new_clip];
                chitra_rhi_apply_scissor(r.x, r.y, r.w, r.h);
                active_clip = new_clip;
            }

            // 3. Apply Pipeline or Texture Bindings if changed
            uint16_t new_texture = (uint16_t)(key & 0x1FFF);
            if (pipeline_id != active_pipeline || new_texture != active_texture) {
                chitra_rhi_apply_pipeline(pipeline_id);
                chitra_rhi_apply_bindings(pipeline_id, new_texture);
                active_pipeline = pipeline_id;
                active_texture  = new_texture;
            }

            // Update state tracker so subsequent matching elements DO NOT flush
            previous_gpu_state = current_gpu_state;
        }

        // 4. Emit indices for the active element into the 32-bit Index Buffer
        Chitra_CommandSlot* slot = &command_table.slots[cmd_id];
        uint32_t v_start = slot->start_vertex_index;
        uint32_t v_count = slot->vertex_count;
        uint32_t quads   = v_count / 4;

        for (uint32_t q = 0; q < quads; q++) {
            emit_quad_indices(&s_index_buffer[index_write_cursor], v_start + (q * 4));
            index_write_cursor += 6;
            batch_index_count  += 6;
        }
    }

    // 5. Final flush for the remaining accumulated batch
    if (batch_index_count > 0) {
        chitra_rhi_draw(batch_base_index, batch_index_count, 1);
    }
}
```

### Execution Trace Example
Suppose an application renders 4 elements:
* **Element 0:** Panel background (DrawOrder=10, Clip=0, Pipeline=Shape, Texture=0)
* **Element 1:** Card background (DrawOrder=20, Clip=0, Pipeline=Shape, Texture=0)
* **Element 2:** Card label text (DrawOrder=25, Clip=0, Pipeline=Text, Texture=Atlas)
* **Element 3:** Card subtitle text (DrawOrder=30, Clip=0, Pipeline=Text, Texture=Atlas)

1. **Element 0:** `state != previous`. Binds Shape pipeline. Accumulates 6 indices (`batch_count = 6`). `prev = State Shape`.
2. **Element 1:** `state == previous` (Clip, Pipeline, Texture match!). **No flush occurs.** Accumulates 6 indices (`batch_count = 12`).
3. **Element 2:** `state != previous` (switched to Text pipeline). **Flushes 12 indices for Elements 0 & 1 in a single draw call.** Binds Text pipeline & font atlas. Accumulates 6 indices (`batch_count = 6`). `prev = State Text`.
4. **Element 3:** `state == previous` (both are Text). **No flush occurs.** Accumulates 6 indices (`batch_count = 12`).
5. **Loop completes:** Final flush draws 12 indices for Elements 2 & 3 in a single draw call.

**Total Draw Calls:** Exactly **2** (one draw call for all shapes, one draw call for all text).

---

## 7. Next Steps

To see how fonts, dynamic text measurement, and vector SVG assets are rasterized and fed into this pipeline, continue to:
* **[Chapter 04: Asset Subsystems (Fonts and Vectors)](04_asset_subsystems.md)**: Modular font backends, the single-upload atlas rule, decoupled text metrics, and `earcut.h` SVG triangulation.
