# Chapter 08: Visual Lifecycles and Flowcharts

## 1. Overview of the Engine Data Flow

This chapter illustrates the runtime mechanics of the Chitra 2D engine through visual flowcharts and sequence diagrams. It models the exact lifecycle of element handles, frame pacing, microsecond Radix Sorting, hole-skipping index generation, and state-aware batch coalescing.

```
+------------------------------------------------------------------------+
|                          HIGH-LEVEL DATA FLOW                          |
+------------------------------------------------------------------------+
  Client Application
         │
         │  1. chitra_create() / Property Setters
         ▼
  RAM: CommandTable (16B slots) + Staging Arrays (40B/20B vertices)
         │
         │  2. Collect active slots into 16B Chitra_SortEntry array
         ▼
  RAM: 8-Pass LSD Radix Sort (Key: [DrawOrder:30 | Clip:16 | Pipe:5 | Tex:13])
         │
         │  3. Stream 32-bit Index Buffer (Skip dead holes in O(0))
         ▼
  RAM: State-Aware Batch Coalescing (Bitmask: 0x00000003FFFFFFFF)
         │
         │  4. Minimal GPU Draw Dispatch
         ▼
  VRAM: Sokol GFX Render Pass (Draw Calls, Scissors, Frame Presentation)
+------------------------------------------------------------------------+
```

---

## 2. Element Lifecycle: Reservation to Destruction

The diagram below traces the complete lifecycle of an element handle from initial slot reservation through dynamic property mutation to zero-cost destruction.

```mermaid
flowchart TD
    Start["Client calls chitra_create(pipeline)"] --> CheckFree{"Is free_list empty?"}
    
    CheckFree -- "Yes" --> GrowTable["Take new index: command_table.count++"]
    CheckFree -- "No" --> PopFree["Pop recycled index from free_list stack"]
    
    GrowTable --> InitSlot["Initialize CommandTable.slots[handle]"]
    PopFree --> InitSlot
    
    InitSlot --> ReturnHandle["Return pure 32-bit handle to client"]
    
    ReturnHandle --> Configure["Client calls setters: bounds, color, content, wrap"]
    
    Configure --> RAMWrite["O(1) Direct Memory Write into staging_arrays[pipeline]"]
    RAMWrite --> PackKey["Pack sort_key: Draw Order (30b), Clip (16b), Pipeline (5b), Texture (13b)"]
    
    PackKey --> ActiveState["Element Active in System RAM"]
    
    ActiveState --> Mutate{"Scene change?"}
    Mutate -- "Position/Color Update" --> Overwrite["O(1) In-place vertex overwrite in staging buffer"]
    Overwrite --> MarkDirty["chitra_mark_dirty() triggers 3-frame flush"]
    
    Mutate -- "Text Shrink" --> Shrink["Overwrite quads in place, record trailing dead hole"]
    Shrink --> MarkDirty
    
    Mutate -- "Text Growth" --> Grow["Abandon old quads as dead hole, append at cursor"]
    Grow --> MarkDirty
    
    Mutate -- "Visibility = False" --> Hide["Set slot vertex_count = 0 (O(0) cost, zero memmove)"]
    Hide --> MarkDirty
    
    Mutate -- "chitra_destroy(handle)" --> Destroy["Set vertex_count = 0, add to dead_count"]
    Destroy --> PushFree["Push handle onto free_list stack"]
    PushFree --> MarkDirty
```

### Lifecycle Highlights
1. **$O(1)$ Slot Allocation:** `chitra_create()` acquires an ID in $O(1)$ time without requiring upfront parameters.
2. **Dense LIFO Packing:** Reusing recently destroyed IDs via the `free_list` stack keeps active handles clustered near index `0`, minimizing memory cache footprint.
3. **Zero Memory Moves:** Dynamic text resizes and deletions leave dead holes in staging buffers; vertices are never shifted via `memmove()`.

---

## 3. Frame Execution and GPU Dispatch Pipeline

The diagram below details the per-frame execution pipeline executed when the client presents a frame.

```mermaid
flowchart TD
    FrameStart["HAL Frame Callback (on_frame)"] --> CheckDirty{"Is scene dirty?"}
    
    CheckDirty -- "No (Static Frame)" --> VSyncCheck{"Target FPS Mode?"}
    VSyncCheck -- "VSync (target_fps = 0)" --> SleepWait["chitra_idle_wait(): MsgWaitForMultipleObjectsEx / nanosleep"]
    SleepWait --> TrueZero["0% CPU / 0% GPU Engine Suspension"]
    VSyncCheck -- "Manual Cap (target_fps > 0)" --> PreciseSleep["chitra_sleep_ms() to frame deadline"]
    
    CheckDirty -- "Yes (Dirty Frame)" --> ScanSlots["Scan CommandTable slots (0 to count)"]
    
    ScanSlots --> FilterAlive{"slot.vertex_count > 0?"}
    FilterAlive -- "Yes" --> EmitSort["Emit Chitra_SortEntry: sort_key + command_id"]
    FilterAlive -- "No" --> SkipDead["Skip dead slot (0 CPU cost)"]
    
    EmitSort --> RadixPass["Execute 8-Pass LSD Radix Sort on 16-byte SortEntry array"]
    SkipDead --> CheckDone{"All slots scanned?"}
    EmitSort --> CheckDone
    CheckDone -- "No" --> ScanSlots
    CheckDone -- "Yes" --> RadixPass
    
    RadixPass --> ResetBatch["Initialize batch state: prev_state = ~0, batch_count = 0"]
    ResetBatch --> DispatchLoop["Iterate sorted entries (i = 0 to active_count)"]
    
    DispatchLoop --> ExtractState["Extract GPU state: state = key & 0x00000003FFFFFFFF"]
    ExtractState --> StateCompare{"state == prev_state?"}
    
    StateCompare -- "Different State" --> FlushPrev["Flush active batch: rhi_draw()"]
    FlushPrev --> ApplyState["Apply new Scissor, bind Pipeline, bind Texture"]
    ApplyState --> UpdatePrev["prev_state = state"]
    UpdatePrev --> EmitIndices["Emit 6 indices per quad into 32-bit Index Buffer"]
    
    StateCompare -- "Same State" --> EmitIndices
    
    EmitIndices --> CheckLoopDone{"More sorted entries?"}
    CheckLoopDone -- "Yes" --> DispatchLoop
    CheckLoopDone -- "No" --> FinalFlush["Final flush: rhi_draw() remaining indices"]
    
    FinalFlush --> Present["RHI end_frame(): GPU Swapchain Presentation"]
    Present --> Countdown["Decrement s_dirty_countdown (3-frame flush)"]
```

### Pipeline Highlights
1. **Microsecond Sorting:** The 8-pass LSD Radix Sort streams flat 16-byte `Chitra_SortEntry` structs through L1 data cache without conditional branch instructions.
2. **Hole-Skipping Index Stream:** Indices are emitted only for active geometry (`vertex_count > 0`). Dead memory holes in RAM and VRAM are naturally bypassed by the index buffer at zero GPU cost.
3. **State Coalescing:** The 34-bit mask (`0x00000003FFFFFFFF`) strips the top 30-bit Draw Order. Adjacent elements on differing visual layers coalesce into the same draw call if their Scissor Clip, Pipeline, and Texture match.

---

## 4. Typography and Single-Upload Atlas Synchronization

The diagram below illustrates the decoupling between font layout metrics, dynamic glyph rasterization, and GPU texture atlas updates.

```mermaid
sequenceDiagram
    autonumber
    actor Client as Client Layout Engine
    participant Font as Chitra Font Subsystem
    participant Atlas as FontStash Atlas (RAM)
    participant Staging as Text Staging Buffer (RAM)
    participant GPU as GPU VRAM (Sokol GFX)

    Note over Client,Font: Phase 1: Pure CPU Text Measurement (Layout Pass)
    Client->>Font: chitra_measure_text(font, size, spacing, text)
    Font->>Font: Read font glyph advance metrics
    Font-->>Client: Return bounding width & height (Zero vertices, Zero GPU calls)

    Note over Client,Staging: Phase 2: Retained Handle Configuration
    Client->>Font: chitra_text_set_content(handle, text)
    Font->>Atlas: Check glyph coverage in atlas cache
    opt Missing Glyphs in Atlas
        Font->>Atlas: Rasterize TrueType glyphs into CPU atlas bitmap
        Font->>Atlas: Mark atlas region as dirty
    end
    Font->>Staging: Emit 20-byte Chitra_TextVertex quads into text staging array

    Note over Font,GPU: Phase 3: Single-Upload Synchronization (Pre-Render Pass)
    Font->>Font: chitra_font_prepare()
    opt Atlas Dirty?
        Font->>GPU: Upload dirty atlas sub-region to VRAM texture (Exactly 1 upload per frame!)
        Font->>Font: Reset atlas dirty flag
    end

    Note over Staging,GPU: Phase 4: Batch Render Dispatch
    Staging->>GPU: Upload text staging vertex buffer
    Staging->>GPU: Emit 32-bit indices & dispatch draw calls
```

### Typography Highlights
1. **Zero-Allocation Layout:** `chitra_measure_text()` evaluates font tables in CPU RAM without generating geometry, allowing Flexbox solvers to calculate layout bounds freely.
2. **Single-Upload Atlas Rule:** New glyphs accumulate in CPU memory during text generation and are uploaded to the GPU texture atlas **exactly once per frame** prior to the render pass.

---

## 5. Memory Compaction and Garbage Collection

The diagram below illustrates the two-tier memory threshold evaluation that compacts staging buffers and recovers memory holes left by text edits or element deletions.

```mermaid
flowchart TD
    TriggerCheck["Element Mutation / Allocation Check"] --> HardCheck{"cursor + alloc > capacity?"}
    
    HardCheck -- "Yes (Hard Limit Hit)" --> ImmediateCompact["Immediate Compaction: Pack active vertices"]
    ImmediateCompact --> CapacityCheck{"active_vertices + alloc > capacity?"}
    CapacityCheck -- "Yes" --> GrowBuffer["Expand staging buffer: capacity *= 1.75x"]
    CapacityCheck -- "No" --> FinalizeCompact["Update slot offsets, reset dead_count = 0"]
    GrowBuffer --> FinalizeCompact
    
    HardCheck -- "No" --> SoftCheck{"dead_count > 50% capacity?"}
    SoftCheck -- "Yes" --> IdleCheck{"Is application currently idle?"}
    IdleCheck -- "Yes (No active user input/gestures)" --> AmortizedCompact["Amortized Compaction: Pack active vertices sequentially"]
    AmortizedCompact --> FinalizeCompact
    IdleCheck -- "No (Active interaction)" --> Postpone["Postpone compaction to avoid micro-stutters"]
    
    SoftCheck -- "No" --> NormalAppend["Append or overwrite geometry normally"]
```

### Compaction Highlights
1. **Hard Limit Protection:** Guarantees OOM prevention by compacting immediately if an allocation would exceed physical buffer capacity.
2. **Amortized Soft Limit:** Compaction triggered by dead hole accumulation (exceeding 50% capacity) executes **only during idle frames**, preventing frame-time spikes during animated dragging or typing gestures.
3. **1.75x Growth Rate:** Provides generous headroom for text appending operations, preventing repeated reallocations.

---

## 6. Architectural Synthesis and Conclusion

The 8 chapters of this manual document a coherent, high-performance 2D engine architecture built upon five non-negotiable principles:

1. **Pure Data-Oriented Design (DOD):** Memory is organized in flat, contiguous arrays matched to 64-byte CPU cache lines, completely eliminating pointer chasing.
2. **Decoupled 30-Bit Draw Order Key:** Separating the 32-bit handle from the 64-bit sort key eliminates artificial Z-Index ceilings, unlocking 1.07 billion layers matching the capacity of the 32-bit index buffer.
3. **Zero-Cost Hole-Skipping:** Dynamic text resizes and element deletions abandon vertices in place; the 32-bit index buffer naturally skips dead holes without expensive `memmove()` CPU stalls.
4. **State-Aware Draw Batch Coalescing:** Evaluating GPU state via the 34-bit bitmask (`0x00000003FFFFFFFF`) merges quads across different layers into the minimum theoretical number of GPU draw calls.
5. **True 0% Idle Frame Pacing:** Platform waitable objects suspend CPU and GPU execution completely when the scene is static, paired with a 3-frame flush for multi-buffered display compositors.

### Manual Navigation
* **[01. Architecture Overview](01_architecture_overview.md)**: System layers, modular pillars, and client contracts.
* **[02. Memory Model and Data Structures](02_memory_model_and_structures.md)**: Dynamic 2-table model and per-pipeline staging arrays.
* **[03. Sorting and Draw Batching Pipeline](03_sorting_and_batch_pipeline.md)**: 64-bit Radix Sort keys and 34-bit state coalescing.
* **[04. Asset Subsystems (Fonts and Vectors)](04_asset_subsystems.md)**: Typography, atlas synchronization, and SVG triangulation.
* **[05. Source Tree and File Responsibilities](05_file_responsibilities.md)**: Physical directory layout and header boundaries.
* **[06. API Reference and Client Integration](06_api_reference_and_integration.md)**: C99 API signatures, reservation handles, and event systems.
* **[07. Architectural Decision Records (ADRs)](07_architectural_decisions.md)**: Formal rationale and engineering trade-offs.
* **[08. Visual Lifecycles and Flowcharts](08_visual_flowcharts.md)**: Visual sequence diagrams and data flow maps.
