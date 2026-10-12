# Chapter 01: Architecture Overview

## 1. Executive Summary and Engine Mission

Chitra is an ultra-high-performance 2D rendering engine implemented in pure C99. Designed from the ground up on modern Data-Oriented Design (DOD) principles, Chitra bridges the gap between retained-mode and immediate-mode graphics paradigms.

Modern graphical applications and user interfaces typically suffer from one of two architectural pitfalls:

1. **The Retained Mode Trap:** Traditional retained renderers mirror the UI layout tree in the rendering backend. This results in duplicate scene-graphs, pointer-heavy tree walks, memory fragmentation, and constant cache thrashing. Unchanged frames still burn CPU cycles synchronizing state across layers.
2. **The Immediate Mode Trap:** Traditional immediate-mode renderers require the application to re-traverse the entire UI hierarchy and regenerate all vertex geometry on every single frame. For complex desktop applications where screens sit idle for long periods, this constantly consumes power, CPU cycles, and memory bandwidth.

### The Chitra Breakthrough
Chitra resolves both failure modes simultaneously:

* **O(0) CPU Cost for Static Frames:** Unchanged UI elements require zero CPU processing per frame. When the display state is static, Chitra skips vertex generation, sorting, and buffer uploads entirely.
* **O(1) In-Place Dirty Updates:** When a client application updates an element's position, size, corner radius, or color, Chitra overwrites contiguous memory directly at the element's existing staging offset. There are no scene-tree traversals and no memory moves.
* **Unified 2-Table Memory Model:** All elements are managed via a contiguous 16-byte Command Table (Slot Map) and a LIFO recycling stack. Memory scales dynamically without hardcoded limits.
* **32-Bit Index Buffer Hole-Skipping:** Resized or deleted elements leave dead memory holes in system RAM and GPU buffers. Chitra naturally bypasses these holes during index buffer generation, eliminating expensive per-frame memory shifting.
* **Microsecond State Coalescing:** Draw calls are batched via a compact 64-bit Radix Sort key that groups compatible primitives across different visual layers without breaking draw batches on layer changes.

---

## 2. Master System Architecture

Chitra is structured into four horizontally decoupled layers. Swapping graphics APIs, windowing toolkits, or asset parsers requires touching only isolated backend interfaces, keeping the engine core completely stable and reusable across projects.

```
+------------------------------------------------------------------------+
|                   Layer 3: Client Application / UI Logic               |
|  (e.g., Desktop GUI Toolkit, Game UI, or Standalone Tool)              |
|  - Owns widget hierarchy, layout math (Flexbox), event dispatch        |
|  - Holds plain 32-bit Element IDs; tracks dirty flags in logic state   |
+-----------------------------------+-+----------------------------------+
                                    |
+-----------------------------------v------------------------------------+
|                   Layer 2: Chitra Engine Core                          |
|                                                                        |
|  [ Element Management & Slot Map ]                                     |
|  - Command Table: Contiguous 16-byte slots (sort key, offset, count)   |
|  - Recycler Stack: LIFO recycling of reusable slot IDs                 |
|                                                                        |
|  [ Geometry & Staging Buffers ]                                        |
|  - Per-Pipeline Vertex Arrays in System RAM (Shape, Text, Mesh)        |
|  - O(1) in-place memory patching and dead-hole allocation              |
|                                                                        |
|  [ Modular Asset Subsystems ]                                          |
|  - Typography Subsystem: Pluggable FontStash or FreeType/HarfBuzz      |
|  - Vector Subsystem: Polygon path triangulation (earcut)               |
|  - Texture Atlas Manager: Single-upload deferred GPU synchronization   |
|                                                                        |
|  [ Sorting & Batching Pipeline ]                                       |
|  - 64-Bit Radix Sort Key Generator and Microsecond Integer Sort        |
|  - 32-Bit Index Buffer Hole-Skipper and Draw Call Batch Coalescer      |
+-----------------------------------+-+----------------------------------+
                                    | (Generic Draw Batches)
+-----------------------------------v------------------------------------+
|               Layer 1: Render Hardware Interface (RHI)                 |
|  - Abstract hardware interface for GPU resource management             |
|  - CURRENT: Sokol GFX (Direct3D 11, Metal, WebGPU, OpenGL ES)          |
|  - FUTURE:  SDL_GPU (Direct3D 12, Metal, Vulkan)                       |
+-----------------------------------+-+----------------------------------+
                                    | (Presents to native OS window)
+-----------------------------------v------------------------------------+
|                    Layer 0: Platform HAL (Window & Input)              |
|  - Abstract platform interface for windowing, timing, and events       |
|  - CURRENT: Sokol App (Ultra-lean single window, cross-platform)       |
|  - FUTURE:  SDL3 (Multi-window, gamepads, native event waiting)        |
+------------------------------------------------------------------------+
```

---

## 3. Component Relationships and Data Flow

### How the Client Application Interacts with Chitra
The Client Application is the driver of the rendering pipeline. It views Chitra not as a scene graph, but as an immediate-retained memory substrate:

1. **Element Allocation:** When the client instantiates a widget, it requests a slot from Chitra and receives an opaque 32-bit Element ID. Chitra writes initial geometry into its staging buffers and records the element in the Command Table.
2. **Layout Decoupling:** When the client calculates widget geometry (e.g. flexbox auto-sizing), it queries Chitra's text metric APIs to compute dimensions before generating render commands.
3. **Dirty State Dispatch:** During runtime frames, the client evaluates its own dirty flags. Only elements that have moved, resized, or changed color issue update requests. Unchanged elements incur zero CPU work.
4. **Frame Orchestration:** The client demarcates frame boundaries (`begin_frame`, `render`, `end_frame`). Chitra executes sorting and GPU submission only during the render pass.

### Internal Coordination within Chitra Engine Core
Within the core engine, data flows sequentially through four internal modules:

```
[ Client Dirty Update ]
           │
           ▼
┌──────────────────────┐      ┌────────────────────────┐
│  Element Management  │ ───> │ Geometry Staging Array │
│  (Command Table)     │      │ (Overwrites vertices)  │
└──────────────────────┘      └────────────────────────┘
           │
           ▼ (Frame Render Triggered)
┌──────────────────────┐      ┌────────────────────────┐
│  Asset Synchronization│───> │ Texture Atlas Upload   │
│  (Glyphs & Vectors)  │      │ (Single GPU sync)      │
└──────────────────────┘      └────────────────────────┘
           │
           ▼
┌──────────────────────┐      ┌────────────────────────┐
│ 64-Bit Radix Sorter  │ ───> │ 32-Bit Index Stream    │
│ (Orders active keys) │      │ (Skips dead holes)     │
└──────────────────────┘      └────────────────────────┘
           │
           ▼
┌──────────────────────────────────────────────────────┐
│  State Coalescer & Batch Dispatcher                  │
│  (Merges compatible states across visual layers)     │
└──────────────────────────────────────────────────────┘
           │
           ▼
[ RHI Hardware Draw Execution ]
```

1. **Element Management:** Maps Element IDs to array offsets and maintains element life cycles.
2. **Geometry Staging:** Maintains contiguous vertex memory in system RAM, executing in-place writes for same-size updates and appending new blocks when dynamic text expands.
3. **Asset Synchronization:** Gathers newly rasterized glyphs or triangulated vector paths into shared GPU texture atlases, ensuring all asset uploads occur in a single batched transfer.
4. **Sorting and Batching:** Collects pre-compiled sort keys for active elements, executes an integer Radix Sort, streams 32-bit indices while leaping over dead memory holes, and coalesces contiguous commands into the minimum number of hardware draw calls.

### The Render Hardware Interface (RHI) Abstraction
Chitra core contains zero vendor-specific or platform-specific graphics code. It communicates with the underlying graphics hardware strictly through an abstract RHI function table.
* **Separation:** The core engine produces generic draw batches defined by index ranges, active shader pipeline types, texture bindings, and hardware scissor rectangles.
* **Backend Independence:** The active RHI implementation translates these generic batches into driver commands (such as Direct3D 11 draw calls, Metal render encoders, or Vulkan command buffers).

### The Platform HAL Abstraction
Operating system windowing, native event pumps, framebuffer sizing, high-resolution timing, and thread sleep are decoupled behind the Platform Hardware Abstraction Layer (HAL).
* **Separation:** The core engine never invokes native platform APIs directly.
* **Multi-Platform Support:** The HAL allows the same rendering core to run inside desktop windows, mobile applications, web browsers, or embedded hardware.

---

## 4. System Boundaries: The Strict Client-Engine Contract

To prevent architectural drift and maintain system modularity, responsibilities are strictly divided between the Client Application and Chitra:

| Feature / Responsibility | Client Application | Chitra Engine Core |
| :--- | :---: | :---: |
| **Widget Hierarchy & Parent-Child Tree** | Owns (ECS / Tree) | No knowledge |
| **Flexbox / Layout Geometry Math** | Owns | No knowledge |
| **User Input & Event Dispatch** | Owns | Passes raw events |
| **Styling, Themes & Color Resolution** | Owns | Receives raw RGBA8 values |
| **Element Identity** | Stores 32-bit Element ID | Owns Slot Map allocation |
| **Vertex Generation & Struct Layout** | No knowledge | Owns (40B Shape, 20B Text) |
| **Vertex Memory Management** | No knowledge | Owns (Staging RAM / VRAM) |
| **Font Glyph Rasterization & Atlas Packing** | No knowledge | Owns (FontStash / FreeType) |
| **Draw Sorting & Batch Coalescing** | No knowledge | Owns (64-bit Radix Sort) |
| **GPU Draw Dispatch & Pipeline Binding** | No knowledge | Owns (Sokol GFX / SDL_GPU) |

---

## 5. Core Design Rules and Hardware Rationale

Chitra enforces five hard design rules to prevent common performance traps:

### Rule 1: Dedicated Specialized Pipelines (Never a Monolithic "God Shader")
* **The Anti-Pattern:** Packing rounded rectangles, borders, font texture sampling, image sampling, and clipping math into a single monolithic fragment shader. This causes GPU warp thread divergence: cores rasterizing simple text stall while neighboring cores calculate complex analytical Signed Distance Field (SDF) box curves.
* **The Chitra Rule:** Chitra maintains dedicated, specialized shader pipelines:
  1. **Shape Pipeline (SDF):** Pure distance field mathematics. 40 bytes per vertex. Zero texture sampling and zero branching.
  2. **Text Pipeline:** Pure font glyph texture modulation. 20 bytes per vertex. Samples the font alpha atlas directly.
  3. **Image / Texture Pipeline:** Standard texture sampling with tinting. 20 bytes per vertex.
  4. **Mesh / Vector Pipeline:** Flat-colored or gradient triangle meshes from SVG triangulation. 20 bytes per vertex.

### Rule 2: Fixed-Function Hardware Scissoring (Never Shader Discards)
* **The Anti-Pattern:** Inserting `if (out_of_bounds) discard;` into fragment shaders. This disables GPU Early-Z silicon optimizations and forces pixel shaders to execute on occluded fragments before discarding them.
* **The Chitra Rule:** Chitra uses the GPU fixed-function hardware scissor registers. The hardware rasterizer clips fragments before pixel shading begins, preserving GPU fillrate.

### Rule 3: Plain 32-Bit Integer Handles (Never Raw Pointers)
* **The Anti-Pattern:** Holding direct pointers to vertex structs. If staging memory reallocates to grow capacity, every held pointer becomes dangling and causes crashes.
* **The Chitra Rule:** Elements are referenced externally solely by plain 32-bit unsigned integer handles. Chitra internally references vertex data via pipeline-local integer offsets. Buffer reallocations never invalidate client handles.

### Rule 4: Dynamic 2-Table Memory (Never Parallel Tracking Lists)
* **The Anti-Pattern:** Maintaining separate arrays for active elements, inactive elements, dirty flags, and slot lookups. This introduces complex synchronization logic, cache pollution, and swap-and-pop bugs.
* **The Chitra Rule:** Chitra uses exactly two tables: a dynamic Command Table array of 16-byte slots, and a dynamic LIFO recycling stack of slot indices. A slot is active if vertex count is greater than zero and inactive if vertex count is zero.

### Rule 5: Pure Rasterization for Vector & Font Assets
* **The Anti-Pattern:** Relying on Multi-channel Signed Distance Fields (MSDF) for text or heavy C++ software rasterizers (like ThorVG) for vector graphics. MSDF introduces visual smoothing artifacts on acute angles and requires runtime fragment computation. Heavy software rasterizers bloat binary size and stall the CPU.
* **The Chitra Rule:** Fonts are rasterized directly to sharp pixel bitmaps at the exact requested layout size into an alpha atlas. Vector paths are parsed and triangulated via lightweight C triangulation (`earcut.h`), feeding directly into Chitra's 20-byte mesh pipeline as hardware-accelerated triangles.

---

## 6. Frame Pacing and Zero-Percent Idle Architecture

A critical performance metric for Chitra is energy efficiency: an idle desktop window should consume zero milliwatts of unnecessary power.

### Event-Driven vs. Polling Frame Loops
* **Continuous Callback Loop:** Under continuous frame callbacks, the frame loop wakes up once per display refresh (e.g., 60 Hz or 144 Hz). While Chitra skips unchanged GPU uploads, the CPU still wakes up for the callback.
* **Event-Driven Sleeping:** For true zero-power consumption, Chitra's HAL architecture supports event-driven suspension (such as Windows `MsgWaitForMultipleObjectsEx` or SDL3 `SDL_WaitEvent`). The thread sleeps until OS input events, window messages, or scheduled timers occur.

### Swapchain Multi-Buffering Synchronization
When a visual state change occurs, a single redraw is insufficient for multi-buffered display swapchains. 
* Modern operating systems use double or triple buffering in their desktop window compositors (such as DWM on Windows).
* When state changes, Chitra activates a 3-frame synchronization countdown. It renders and presents across 3 consecutive display frames to ensure every back-buffer in the swapchain chain contains the new visual state.
* Once the counter reaches zero, Chitra fully suspends rendering until the next state change.

---

## 7. Next Steps

To dive deeper into the memory structures and implementation details of the engine, continue to:
* **[Chapter 02: Memory Model and Data Structures](02_memory_model_and_structures.md)**: Dynamic 2-table model, 16-byte command slot, per-pipeline staging arrays, and compaction mechanics.
* **[Chapter 03: Sorting and Draw Batching Pipeline](03_sorting_and_batch_pipeline.md)**: 64-bit Radix Sort keys, 32-bit index buffer generation, and GPU state coalescing.
* **[Chapter 04: Asset Subsystems (Fonts and Vectors)](04_asset_subsystems.md)**: Modular font backends, single-upload atlas synchronization, and vector path triangulation.
