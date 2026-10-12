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
│   ├── chitra_font.h              # Typography subsystem interface contract
│   ├── chitra_cmd.h               # Internal command buffer and batching interface
│   └── chitra_arena.h             # Dynamic memory arena utilities
├── src/                           # Core engine implementation
│   ├── chitra.c                   # Engine initialization, lifecycle, and API router
│   ├── chitra_cmd.c               # Command buffer, Radix Sort, 32-bit index generation
│   ├── chitra_font.c              # Typography implementation and font atlas management
│   ├── chitra_arena.c             # Bump-allocation arena implementation
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
      void (*init)(void);
      void (*begin_frame)(int width, int height);
      void (*upload_vertices)(const void* shape_data, size_t shape_size,
                              const void* text_data, size_t text_size,
                              const uint16_t* index_data, size_t index_size);
      void (*set_scissor)(int x, int y, int w, int h);
      void (*draw_batch)(const Chitra_DrawBatch* batch);
      uint16_t (*create_texture)(int width, int height);
      void (*update_texture)(uint16_t id, const unsigned char* data, int width, int height);
      void (*destroy_texture)(uint16_t id);
      void (*end_frame)(void);
      void (*cleanup)(void);
  } Chitra_RHI_API;
  ```
* **Responsibility:** Abstracts all GPU driver operations (pipeline binding, vertex buffer uploading, hardware scissor configuration, draw dispatch, and texture allocation). The core engine interacts exclusively with this interface.

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

### E. [include/chitra_cmd.h](../../include/chitra_cmd.h)
* **Role:** Internal interface for the engine command buffer and batching pipeline. Opaque to client applications.
* **Responsibility:** Exposes internal lifecycle functions (`chitra_cmd_init`, `chitra_cmd_clear`, `chitra_cmd_prepare`, `chitra_cmd_draw`), clip stack queries, and accessors for raw vertex buffers used during GPU uploads.

### F. [include/chitra_arena.h](../../include/chitra_arena.h)
* **Role:** Dynamic memory arena structure and memory utility functions.
* **Responsibility:** Provides sub-microsecond bump allocation (`chitra_arena_push`) and instant memory clearing (`chitra_arena_clear`) for frame-lifetime data.

---

## 3. Core Engine Implementation (`src/`)

The files in `src/` contain the platform-independent core logic of Chitra. They do not depend on external windowing or graphics APIs.

### A. [src/chitra.c](../../src/chitra.c)
* **Module:** Engine Coordinator & Public Router
* **Responsibilities:**
  1. **Backend Discovery:** Retrieves registered vtables via `chitra_hal_get_api()` and `chitra_rhi_get_api()`.
  2. **Application Lifecycle:** Coordinates engine startup via `chitra_run()`, delegating the event loop to the registered HAL.
  3. **Internal Synchronization:** Exposes `chitra_internal_init()`, `chitra_internal_prepare()`, and `chitra_internal_term()` invoked by HAL frame hooks.
  4. **API Routing:** Forwards high-level drawing calls (`chitra_draw_rect`, `chitra_draw_text`) to internal subsystems (`chitra_cmd` and `chitra_font`).

### B. [src/chitra_cmd.c](../../src/chitra_cmd.c)
* **Module:** Command Processing, Sorting, and Batching
* **Responsibilities:**
  1. **Dynamic Memory Model:** Owns the `CommandTable` and `free_list` metadata structures and per-pipeline vertex staging buffers.
  2. **Memory Hole Skipping:** Preserves dead memory holes on text append or element resize, emitting index buffer streams that completely bypass dead memory.
  3. **64-Bit Radix Sorting:** Constructs 64-bit integer sort keys (`[ Draw Order (30) | Clip ID (16) | Pipeline ID (5) | Texture ID (13) ]`) paired with `command_id` inside 16-byte `Chitra_SortEntry` structures.
  4. **State Coalescing:** Evaluates the 34-bit state bitmask (`0x00000003FFFFFFFF`) to merge compatible draw calls across differing visual layers into the minimum theoretical number of GPU draw calls.
  5. **Clip Hierarchy Table:** Manages the clip stack, calculating geometric intersections for nested scissor regions and emitting unique 16-bit `Clip ID`s.

### C. [src/chitra_font.c](../../src/chitra_font.c)
* **Module:** Typography and Font Atlas Management
* **Responsibilities:**
  1. **Font Loading:** Loads TTF fonts from local disk files or memory buffers into FontStash contexts.
  2. **Single-Upload Atlas Synchronization:** Implements custom FontStash callback vtables (`fons_render_create`, `fons_render_update`, `fons_render_draw`). Dirty atlas regions accumulate during text layout and are uploaded to GPU VRAM once per frame in `chitra_font_prepare()`.
  3. **Text Glyph Quad Generation:** Uses `fonsTextIter` to iterate glyph metrics and coordinates, emitting 20-byte `Chitra_TextVertex` quads into the text staging buffer.
  4. **Decoupled Text Measurement:** Implements `chitra_measure_text()` using font advance metrics without pushing quads into the frame command queue.

### D. [src/chitra_arena.c](../../src/chitra_arena.c)
* **Module:** Dynamic Memory Bump Allocator
* **Responsibilities:**
  1. **Fast Allocation:** Provides $O(1)$ pointer increments for memory pushes.
  2. **Capacity Growth:** Automatically reallocates underlying buffers using geometric expansion when initial capacities are exceeded.
  3. **Zero Heap Overhead:** Allows clearing thousands of allocations instantly by setting `arena->size = 0` without invoking `free()`.

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
* **Pipeline:** Text & Texture Pipeline (`CHITRA_PIPELINE_TEXTURE`)
* **Vertex Inputs:** Position (`pos`), Texture/Atlas UVs (`uv`), Tint Color (`color`).
* **Vertex Shader:** Transforms logical pixel positions into NDC coordinates.
* **Fragment Shader:** Samples the red channel of the font atlas texture as alpha coverage and multiplies by the tinted vertex color:
  ```glsl
  float alpha = texture(sampler2D(tex, smp), v_uv).r;
  frag_color = vec4(v_color.rgb, v_color.a * alpha);
  ```

### C. [gen/](../../src/backends/shaders/gen)
* Contains pre-generated C headers produced by `sokol-shdc`:
  * `chitra_shape.glsl.h`: Contains byte arrays for HLSL5, Metal, and GLSL shape shaders.
  * `chitra_text.glsl.h`: Contains byte arrays for HLSL5, Metal, and GLSL text shaders.
* Embedded directly into `sokol_rhi.c` via `#include`.

---

## 6. Embedded Third-Party Dependencies (`third_party/`)

All dependencies are maintained as vendored source distributions within `third_party/`, requiring zero external system package installations.

| Subsystem | Location | Description |
| :--- | :--- | :--- |
| **Sokol** | `third_party/sokol/` | Single-file header-only C libraries for modern graphics (`sokol_gfx.h`), windowing (`sokol_app.h`), high-resolution time (`sokol_time.h`), and platform glue (`sokol_glue.h`). |
| **Sokol Tools** | `third_party/sokol-tools/` | Precompiled binary cross-compiler (`sokol-shdc.exe`) used during CMake builds to generate shader headers. |
| **FontStash** | `third_party/fontstash/` | Lightweight single-file font atlas generation library (`fontstash.h`) with Truetype glyph rasterization. |

---

## 7. Standalone Test Harness (`sandbox/`)

### [sandbox/chitra_sandbox.c](../../sandbox/chitra_sandbox.c)
* **Role:** Standalone testing harness for engine validation and feature demonstration.
* **Capabilities:**
  * Tests shape rendering (rectangles, rounded cards, pill buttons, borders).
  * Validates font loading, multi-line wrapped text, and dynamic text measurement.
  * Validates hardware scissor clipping stacks.
  * Tests interactive input handling (mouse clicks, dragging, keyboard navigation).
  * Measures frame pacing and verifies 0% idle frame suspension.
* **Independence:** Builds directly against `libchitra.a` / `chitra.lib` without requiring any external UI framework or third-party runtime.

---

## 8. Architectural Guardrails and Code Rules

Maintainers and AI coding assistants must adhere to the following code placement rules:

1. **Header Boundary Rule:**
   * Files in `include/` must **never** include backend headers (`sokol_gfx.h`, `windows.h`, `d3d11.h`, etc.).
   * Client applications that include `<chitra.h>` must never be exposed to backend-specific definitions.
2. **Backend Modularity Rule:**
   * Core engine files (`chitra_cmd.c`, `chitra_font.c`, `chitra_arena.c`) must **never** call graphics API functions directly. All GPU interactions must route through the `Chitra_RHI_API` vtable.
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
