# Chapter 05: Source Tree and File Responsibilities

## 1. Overview of the Source Organization

Chitra maintains a strictly decoupled, modular repository structure. The codebase is organized to enforce clear physical boundaries between public client interfaces, core engine logic, interchangeable platform and graphics backends, and embedded third-party dependencies.

```
third_party/chitra/
├── CMakeLists.txt                 # Master build configuration and shader compilation targets
├── LICENSE                        # Apache 2.0 open-source license
├── README.md                      # Standalone project overview, setup, and build guide
├── docs/                          # Engine documentation suite
│   ├── index.md                   # Documentation index and architecture overview
│   └── manual/                    # 8-chapter definitive technical manual
├── include/                       # Public API and internal interface contracts
│   ├── chitra.h                   # Public client API (consumed by client UI frameworks)
│   ├── chitra_hal.h               # Hardware Abstraction Layer (HAL) interface contract
│   ├── chitra_rhi.h               # Render Hardware Interface (RHI) contract
│   ├── chitra_table.h             # Slot table and handle kernel interface contract
│   ├── chitra_shape.h             # Dedicated Shape pipeline interface contract
│   ├── chitra_texture.h           # Dedicated Texture pipeline interface contract
│   ├── chitra_font.h              # Dedicated Typography pipeline interface contract
│   ├── chitra_batch.h             # 16-byte SortEntry, LSD Radix Sort, and batching contract
│   └── chitra_clip.h              # Dynamic scissor registry and coordinate mapping
├── src/                           # Core engine implementation
│   ├── chitra.c                   # Engine coordinator, frame hooks, and public API router
│   ├── chitra_table.c             # Slot table kernel, LIFO free_list recycler, and alive state
│   ├── chitra_shape.c             # Dedicated Shape pipeline: SDF staging quads & in-place setters
│   ├── chitra_texture.c           # Dedicated Texture pipeline: image quads & UV mapping
│   ├── chitra_font.c              # Dedicated Typography pipeline: FontStash TTF & text quads
│   ├── chitra_batch.c             # 8-pass LSD Radix Sort, 32-bit index streaming, batch coalescing
│   ├── chitra_clip.c              # Dynamic clip table with recycler stack and scissor mapping
│   └── backends/                  # Modular platform and graphics backends
│       ├── sokol_hal.c            # Sokol App windowing and event dispatch backend
│       ├── sokol_rhi.c            # Sokol GFX hardware rasterization backend
│       ├── sokol_impl.c           # Isolated Sokol single-file C implementation unit
│       └── shaders/               # GLSL shaders and cross-compilation pipeline
│           ├── chitra_shape.glsl  # Analytic SDF shader for rounded boxes and borders
│           ├── chitra_text.glsl   # Alpha-blended text glyph and texture shader
│           └── gen/               # Auto-generated C headers from sokol-shdc
│               ├── chitra_shape.glsl.h
│               └── chitra_text.glsl.h
├── third_party/                   # Embedded dependencies
│   ├── fontstash/                 # Lightweight dynamic font atlas library
│   ├── sokol/                     # Platform, graphics, time, and glue headers
│   └── sokol-tools/               # Precompiled sokol-shdc shader compiler binaries
└── sandbox/                       # Standalone engine test harness
    ├── chitra_sandbox.c           # Sandbox entry point for validating rendering features
    └── Font/                      # Bundled test fonts for the sandbox harness
```

---

## 2. Public Interface Contracts (`include/`)

Headers located in `include/` define the API contracts across the system. They contain zero backend-specific headers (such as Sokol, DirectX, Metal, or Windows headers), ensuring that client applications compile cleanly on any platform.

### A. [include/chitra.h](../../include/chitra.h)
* **Role:** The primary public header exposed to client frameworks and standalone applications.
* **Key Types & Structs:**
  * `Chitra_ShapeVertex` (40 bytes): Stride-matched vertex layout for rectangles, pills, and bordered panels.
  * `Chitra_TextVertex` (20 bytes): Stride-matched vertex layout for font glyphs and textured geometry.
  * `Chitra_ClipRect`: Integer bounding box (`x, y, w, h`) defining hardware scissor regions.
  * `Chitra_Config`: Application configuration struct containing window dimensions, title, frame pacing options (`target_fps`), and user callbacks (`on_init`, `on_frame`, `on_event`).
  * `Chitra_InputEvent`: Unified OS event abstraction covering mouse movements, mouse buttons, scrolling, keyboard input, text character entry, and window resizing.
* **Core Functions:**
  * `chitra_run()`: Initializes engine backends and launches the platform event loop.
  * `chitra_draw_rect()`: Pushes a shape quad (fill color, border color, corner radius, border thickness).
  * `chitra_draw_text()`: Emits a text string rendered from a loaded font.
  * `chitra_measure_text()`: Measures text width and height without visual emission (pure CPU measurement).
  * `chitra_push_clip()` / `chitra_pop_clip()`: Manages nested hardware scissor clipping regions.
  * `chitra_mark_dirty()`: Notifies the engine that visual state has changed, triggering GPU upload and presentation.

### B. [include/chitra_hal.h](../../include/chitra_hal.h)
* **Role:** Defines the Hardware Abstraction Layer (HAL) interface contract.
* **Interface Definition:**
  ```c
  typedef struct {
      void (*init)(const Chitra_Config* config);
      void (*run)(void);
      void (*quit)(void);
  } Chitra_HAL_API;
  ```
* **Responsibility:** Completely isolates window creation, display refresh synchronization, and OS input polling from the core renderer. Swapping from Sokol App to SDL3, GLFW, or an embedded display driver requires providing an alternate implementation of `Chitra_HAL_API` without changing any engine code.

### C. [include/chitra_rhi.h](../../include/chitra_rhi.h)
* **Role:** Defines the Render Hardware Interface (RHI) contract.
* **Interface Definition:**
  ```c
  typedef struct {
      void (*init)(const struct Chitra_Config* config);
      void (*begin_frame)(int width, int height);
      void (*upload_vertices)(uint8_t pipeline, const void* data, size_t size);
      void (*upload_indices)(const uint32_t* index_data, size_t index_size);
      void (*set_scissor)(int x, int y, int w, int h);
      void (*draw_batch)(const Chitra_DrawBatch* batch);
      uint16_t (*create_texture)(int width, int height);
      void     (*update_texture)(uint16_t id, const unsigned char* data, int width, int height);
      uint16_t (*create_texture_rgba)(int width, int height);
      void     (*update_texture_rgba)(uint16_t id, const void* data, int width, int height);
      void     (*update_texture_sub_rgba)(uint16_t id, int dst_x, int dst_y, int w, int h, const void* data);
      void     (*destroy_texture)(uint16_t id);
      void (*end_frame)(void);
      void (*cleanup)(void);
  } Chitra_RHI_API;

  void chitra_rhi_set_api(const Chitra_RHI_API* api);
  const Chitra_RHI_API* chitra_rhi_get_api(void);
  ```
* **Responsibility:** Abstracts all GPU driver operations (pipeline binding, transient vertex/index buffer uploading, hardware scissor configuration, draw dispatch, dynamic texture pool, and sub-rect VRAM updates). The core engine interacts exclusively with this interface. Supports runtime RHI registration (`chitra_rhi_set_api`) for mock headless unit testing.

### D. [include/chitra_font.h](../../include/chitra_font.h)
* **Role:** Defines the modular Typography Subsystem contract.
* **Interface Definition:**
  ```c
  typedef struct {
      void (*init)(void);
      void (*term)(void);
      int  (*load_font)(const char* name, const char* path);
      void (*push_text)(int z_index, int font_id, float size, float wrap_width,
                        float x, float y, const char* text, uint32_t color);
      void (*measure_text)(int font_id, float size, float wrap_width,
                           const char* text, float* out_width, float* out_height);
      void (*prepare)(void);
  } Chitra_Font_API;
  ```
* **Responsibility:** Enables typography engines to be swapped cleanly. Chitra currently provides a FontStash backend and can seamlessly transition to a FreeType + HarfBuzz backend without modifying core command queues or batching pipelines.

### E. [include/chitra_table.h](../../include/chitra_table.h)
* **Role:** Internal interface for the slot table kernel, LIFO handle recycling, and alive state tracking.
* **Responsibility:** Declares `Chitra_CommandSlot` (16 bytes), `Chitra_CommandTable`, `Chitra_FreeList`, slot accessors, and sort key visual state setters.

### F. [include/chitra_shape.h](../../include/chitra_shape.h)
* **Role:** Internal interface for the dedicated Shape pipeline.
* **Responsibility:** Declares Shape quad allocation, staging buffer access, and compaction for SDF rounded boxes, borders, and pills.

### G. [include/chitra_texture.h](../../include/chitra_texture.h)
* **Role:** Public/internal interface for the dedicated Texture Pipeline, dynamic VRAM atlas packing, and spritesheet mapping.
* **Responsibility:** Declares `chitra_texture_load()`, `chitra_texture_load_file()`, `chitra_texture_set_src_rect()`, bounds/UV/tint setters, ref-counting hooks, and atlas introspection getters.

### H. [include/chitra_batch.h](../../include/chitra_batch.h)
* **Role:** Internal interface for sorting, 32-bit index streaming, and batch coalescing.
* **Responsibility:** Declares `Chitra_SortEntry` (16 bytes), the 8-pass LSD Radix Sort pipeline, hole-skipping index stream generation, and `Chitra_DrawBatch`.

### I. [include/chitra_clip.h](../../include/chitra_clip.h)
* **Role:** Internal interface for dynamic scissor clipping and high-DPI coordinate mapping.
* **Responsibility:** Declares dynamic `clip_table` with recycler stack, nested scissor intersection math, and physical pixel scaling.

---

## 3. Core Engine Implementation (`src/`)

The files in `src/` contain the platform-independent core logic of Chitra. They do not depend on external windowing or graphics APIs.

### A. [src/chitra.c](../../src/chitra.c)
* **Module:** Engine Coordinator & Public Router
* **Responsibilities:**
  1. **Backend Discovery:** Retrieves registered vtables via `chitra_hal_get_api()` and `chitra_rhi_get_api()`.
  2. **Application Lifecycle:** Coordinates engine startup via `chitra_run()` or embedded `chitra_init()` / `chitra_render()`.
  3. **Internal Synchronization:** Exposes `chitra_internal_init()`, `chitra_internal_prepare()`, and `chitra_internal_term()` invoked by HAL frame hooks.
  4. **API Routing:** Forwards high-level calls to internal subsystems (`chitra_table`, `chitra_shape`, `chitra_texture`, `chitra_font`, `chitra_batch`, `chitra_clip`).

### B. [src/chitra_table.c](../../src/chitra_table.c)
* **Module:** Slot Table Kernel & Handle Lifecycle
* **Responsibilities:**
  1. **Command Table:** Owns the `Chitra_CommandSlot` table and explicit boolean alive tracking array.
  2. **LIFO Recycler:** Owns `Chitra_FreeList` for recycling destroyed handles in $O(1)$.
  3. **Delegated Allocation:** Directs `chitra_create()` to dedicated pipeline allocators (`chitra_shape_alloc`, `chitra_texture_alloc`).
  4. **State Modifiers & Introspection:** Mutates visual sort keys (Draw Order, Clip, Texture ID) in RAM and implements runtime debug getters (`chitra_get_texture_id`, `chitra_get_clip_id`, `chitra_get_pipeline_id`, `chitra_get_draw_order`).

### C. [src/chitra_shape.c](../../src/chitra_shape.c)
* **Module:** Dedicated Shape Pipeline
* **Responsibilities:**
  1. **Staging Buffer:** Owns `s_shape_pipe` (40-byte `Chitra_ShapeVertex` staging array).
  2. **Immediate Quad Allocation:** Reserves 4 vertices once on shape creation.
  3. **Unconditional $O(1)$ Setters:** Implements `chitra_rect_set_bounds()`, `chitra_rect_set_radius()`, `chitra_rect_set_border()`, `chitra_rect_set_fill_color()`, and `chitra_rect_set_border_color()` as pure, direct RAM overwrites with zero cursor manipulation.
  4. **Compaction:** Compacts dead shape quads during idle frames.

### D. [src/chitra_texture.c](../../src/chitra_texture.c)
* **Module:** Dynamic RGBA Texture & VRAM Atlas Pipeline
* **Responsibilities:**
  1. **Staging Buffer:** Owns `s_texture_pipe` (20-byte `Chitra_TextVertex` staging array sharing vertex layout with Text).
  2. **Hybrid Direct-to-VRAM Loading:** Packs small images ($\le 512\times 512$) into active dynamic 1024x1024 atlases via `stb_rect_pack.h` directly to GPU VRAM with zero CPU staging memory; allocates standalone GPU textures for large images ($> 512\times 512$).
  3. **Multi-Page Atlas Growth:** Dynamically allocates new atlas pages upon cursor overflow without invalidating existing elements.
  4. **Ref-Counting & 35% Auto-Evacuation:** Tracks living quad references and accumulated dead pixel area; automatically flags and evicts dead textures when dead ratio reaches $\ge 35\%$.
  5. **Direct File Loading:** Integrates `stb_image.h` (`chitra_texture_load_file()`) for 1-call PNG/JPG/BMP ingestion.
  6. **Spritesheet Mapping:** Implements `chitra_texture_set_src_rect()` to map sub-rectangles across both standalone textures and atlas-packed regions seamlessly.

### E. [src/chitra_font.c](../../src/chitra_font.c)
* **Module:** Dedicated Typography Pipeline
* **Responsibilities:**
  1. **Staging Buffer:** Owns `s_text_pipe` (20-byte `Chitra_TextVertex` staging array).
  2. **Dynamic Quads:** Resizes quads in-place or appends at cursor when text length changes.
  3. **Font Loading & Atlas Sync:** Loads TTF fonts into FontStash contexts and synchronizes dynamic atlas textures to GPU VRAM once per frame.
  4. **Decoupled Metrics:** Computes pure CPU text measurements (`chitra_measure_text()`, font metrics) with zero vertex allocation.

### F. [src/chitra_batch.c](../../src/chitra_batch.c)
* **Module:** LSD Radix Sorter & 32-Bit Index Stream Engine
* **Responsibilities:**
  1. **Active Slot Scanning:** Collects alive elements (`vertex_count > 0`) into cache-line aligned 16-byte `Chitra_SortEntry` structs.
  2. **8-Pass LSD Radix Sort:** Orders active elements across 64-bit integer keys in microseconds without pointer dereferences.
  3. **32-Bit Index Buffer Hole Skipping:** Emits 32-bit indices directly from `start_vertex_index`, leaping over dead memory holes with zero GPU penalty.
  4. **34-Bit State Coalescing:** Evaluates `key & 0x00000003FFFFFFFFULL` to merge compatible quads across differing Draw Orders into unified GPU draw calls.

### G. [src/chitra_clip.c](../../src/chitra_clip.c)
* **Module:** Dynamic Scissor Registry
* **Responsibilities:**
  1. **Dynamic Allocation & Recycling:** Starts at 16 clip regions, grows 1.5x on demand, and recycles destroyed clip IDs via `s_clip_free_list`.
  2. **Nested Intersections:** Computes geometric intersections for parent/child clipping hierarchies.
  3. **High-DPI Translation:** Converts logical screen coordinates to physical framebuffer pixels.

---

## 4. Modular Backends (`src/backends/`)

Backend files implement the interface contracts defined in `include/`. They contain all direct calls to third-party libraries and OS-specific APIs.

### A. [src/backends/sokol_hal.c](../../src/backends/sokol_hal.c)
* **Module:** Platform Windowing and Frame Pacing (Sokol App)
* **Responsibilities:**
  1. **Window Creation & Event Polling:** Configures `sapp_desc`, initializes desktop application windows, and translates native Sokol events into `Chitra_InputEvent` structs.
  2. **High-Resolution Frame Pacing:** Implements `chitra_sleep_ms()` using sub-millisecond waitable timers on Windows (`CREATE_WAITABLE_TIMER_HIGH_RESOLUTION`) and POSIX `nanosleep()` on Linux and macOS.
  3. **0% Idle Frame Suspension:** Implements `chitra_idle_wait()`. When the UI state is unchanged and no user interactions occur, the thread suspends completely via `MsgWaitForMultipleObjectsEx(INFINITE)` on Windows or sleeps on POSIX, eliminating idle CPU and GPU power consumption.
  4. **Multi-Buffer Swapchain Flush:** Manages a 3-frame countdown (`s_dirty_countdown = 3`) when visual state changes, ensuring all swapchain back-buffers are updated before entering suspension.

### B. [src/backends/sokol_rhi.c](../../src/backends/sokol_rhi.c)
* **Module:** Hardware Rasterization Backend (Sokol GFX)
* **Responsibilities:**
  1. **GPU Pipeline Creation:** Compiles and binds Sokol GFX pipelines for the Shape (SDF) and Text/Texture shader programs.
  2. **Buffer Management:** Maintains GPU vertex buffers (`chitra_shape_vbuf`, `chitra_text_vbuf`) and unified 32-bit index buffers (`chitra_ibuf`). Uses staging copy buffers for efficient CPU-to-GPU data streaming.
  3. **Hardware Scissor Execution:** Sets hardware clipping boundaries via `sg_apply_scissor_rect()`, automatically translating logical coordinates to physical framebuffer pixels for high-DPI displays.
  4. **Draw Dispatch:** Executes draw calls (`sg_draw()`) based on batch element offsets computed during state coalescing.
  5. **Dynamic Texture Management:** Allocates and updates GPU textures for font atlases and user images.

### C. [src/backends/sokol_impl.c](../../src/backends/sokol_impl.c)
* **Module:** Isolated Header Implementation Translation Unit
* **Responsibilities:**
  1. **Compilation Isolation:** Defines the implementation macros (`SOKOL_IMPL`, `SOKOL_GFX_IMPL`, `SOKOL_APP_IMPL`, `SOKOL_GLUE_IMPL`, `SOKOL_LOG_IMPL`) in a single dedicated compilation unit.
  2. **Build Optimization:** Isolates heavy header parsing to a single object file, preventing long recompilation times when modifying engine source files.

---

## 5. Shaders and Code Generation (`src/backends/shaders/`)

Shaders are authored in GLSL and cross-compiled to platform-native shading languages using `sokol-shdc`.

```
                    sokol-shdc Cross-Compilation
                                 |
         +-----------------------+-----------------------+
         |                                               |
         v                                               v
   chitra_shape.glsl                               chitra_text.glsl
  (Analytic SDF Shape)                           (Alpha-Blended Text)
         |                                               |
         +-----------------------+-----------------------+
                                 |
          Cross-compiled to: HLSL5, Metal, GLSL 4.10, GLES 3
                                 |
                                 v
                     gen/chitra_shape.glsl.h
                     gen/chitra_text.glsl.h
```

### A. [chitra_shape.glsl](../../src/backends/shaders/chitra_shape.glsl)
* **Pipeline:** Shape Pipeline (`CHITRA_PIPELINE_SHAPE`)
* **Vertex Inputs:** Position (`pos`), Box UVs (`uv`), Dimensions (`size`), Radius & Border (`radius_border`), Fill Color (`fill_color`), Border Color (`border_color`).
* **Vertex Shader:** Transforms logical pixel positions into Normalized Device Coordinates (NDC) using the display resolution uniform.
* **Fragment Shader:** Evaluates an analytic Signed Distance Field (SDF) representing a rounded rectangle:
  ```glsl
  float rounded_box_sdf(vec2 center_pos, vec2 size, float radius) {
      vec2 q = abs(center_pos) - size + vec2(radius);
      return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - radius;
  }
  ```
  Applies `smoothstep()` for sub-pixel anti-aliasing. If border thickness is greater than zero, evaluates an inner distance field to blend border and fill colors.

### B. [chitra_text.glsl](../../src/backends/shaders/chitra_text.glsl)
* **Pipeline:** Unified Text & Texture Pipelines (`CHITRA_PIPELINE_TEXT` and `CHITRA_PIPELINE_TEXTURE`)
* **Vertex Inputs:** Position (`pos`), Texture/Atlas UVs (`uv`), Tint Color (`color`). Uses identical 20-byte `Chitra_TextVertex` stride.
* **Vertex Shader (`vs_text`):** Transforms logical pixel positions into NDC coordinates for both text glyphs and image quads.
* **Fragment Shaders:**
  * `fs_text`: Samples the red channel of the font atlas texture as alpha coverage and multiplies by vertex color:
    ```glsl
    float alpha = texture(sampler2D(tex, smp), v_uv).r;
    frag_color = vec4(v_color.rgb, v_color.a * alpha);
    ```
  * `fs_texture`: Samples 4-channel RGBA texture and modulates with vertex tint color:
    ```glsl
    frag_color = v_color * texture(sampler2D(tex, smp), v_uv);
    ```
* **Programs Compiled:** `@program text vs_text fs_text` and `@program texture vs_text fs_texture`.

### C. [gen/](../../src/backends/shaders/gen)
* Contains pre-generated C headers produced by `sokol-shdc`:
  * `chitra_shape.glsl.h`: Contains byte arrays for HLSL5, Metal, and GLSL shape shaders.
  * `chitra_text.glsl.h`: Contains byte arrays for HLSL5, Metal, and GLSL text and texture shaders.
* Embedded directly into `sokol_rhi.c` via `#include`.

---

## 6. Embedded Third-Party Dependencies (`third_party/`)

All dependencies are maintained as vendored source distributions within `third_party/`, requiring zero external system package installations.

| Subsystem | Location | Description |
| :--- | :--- | :--- |
| **Sokol** | `third_party/sokol/` | Single-file header-only C libraries for modern graphics (`sokol_gfx.h`), windowing (`sokol_app.h`), high-resolution time (`sokol_time.h`), and platform glue (`sokol_glue.h`). |
| **Sokol Tools** | `third_party/sokol-tools/` | Precompiled binary cross-compiler (`sokol-shdc.exe`) used during CMake builds to generate shader headers. |
| **FontStash** | `third_party/fontstash/` | Lightweight single-file font atlas generation library (`fontstash.h`) with glyph rasterization. |
| **STB Libraries** | `third_party/stb/` | Public domain single-header utilities: `stb_image.h` (image file loading), `stb_rect_pack.h` (2D bin packing for dynamic VRAM atlas), and `stb_truetype.h` (embedded font parsing). |

---

## 7. Test Harnesses and Validation (`sandbox/` and `tests/`)

### A. [sandbox/chitra_sandbox.c](../../sandbox/chitra_sandbox.c)
* **Role:** Interactive standalone visual testing harness for engine validation and feature demonstration.
* **Capabilities:**
  * Tests shape rendering (rectangles, rounded cards, pill buttons, borders).
  * Validates font loading, multi-line wrapped text, and dynamic text measurement.
  * Validates hardware scissor clipping stacks.
  * Tests interactive input handling (mouse clicks, dragging, keyboard navigation).
  * Measures frame pacing and verifies 0% idle frame suspension.
* **Independence:** Builds directly against `libchitra.a` / `chitra.lib` without requiring any external UI framework.

### B. [tests/test_texture.c](../../tests/test_texture.c)
* **Role:** Automated headless unit test suite verifying the Texture Pipeline and Dynamic Atlas Subsystem.
* **Capabilities:**
  * Tests mock headless RHI registration (`chitra_rhi_set_api`).
  * Validates standalone texture allocation for images $> 512\times 512$.
  * Validates dynamic 1024x1024 atlas bin-packing and direct-to-VRAM sub-rectangle uploads (`update_texture_sub_rgba`).
  * Validates multi-page atlas growth, cursor overflow, and page spillover.
  * Verifies living quad reference counting and 35% dead-area auto-evacuation.
  * Verifies spritesheet and local rect UV coordinate translation (`chitra_texture_set_src_rect`).
  * Verifies image file ingestion via `stb_image` (`chitra_texture_load_file`).
  * Validates runtime introspection debug getters (`chitra_get_texture_id`, `chitra_get_clip_id`, `chitra_get_pipeline_id`, `chitra_get_draw_order`).

---

## 8. Architectural Guardrails and Code Rules

Maintainers and AI coding assistants must adhere to the following code placement rules:

1. **Header Boundary Rule:**
   * Files in `include/` must **never** include backend headers (`sokol_gfx.h`, `windows.h`, `d3d11.h`, etc.).
   * Client applications that include `<chitra.h>` must never be exposed to backend-specific definitions.
2. **Backend Modularity Rule:**
   * Core engine files (`chitra_table.c`, `chitra_shape.c`, `chitra_texture.c`, `chitra_font.c`, `chitra_batch.c`, `chitra_clip.c`) must **never** call graphics API functions directly. All GPU interactions must route through the `Chitra_RHI_API` vtable.
   * Core engine files must **never** call OS windowing APIs directly. All platform interactions must route through the `Chitra_HAL_API` vtable.
3. **Allocation Rule:**
   * No dynamic memory allocations (`malloc`, `realloc`, `calloc`) are permitted during steady-state idle frames.
   * Frame-lifetime temporary data must utilize pre-allocated scratch buffers or the dynamic `Chitra_Arena`.
4. **Shader Invariant:**
   * All shader modifications must be made in `src/backends/shaders/*.glsl`. Never edit the generated headers in `gen/` manually; regenerate them using `sokol-shdc`.

---

## 9. Next Steps

To examine the exact C function signatures, parameter contracts, and client integration walkthroughs, continue to:
* **[Chapter 06: API Reference and Client Integration](06_api_reference_and_integration.md)**: Function-by-function C reference, lifecycle hooks, and working integration code for client frameworks.
