# Chapter 06: API Reference and Client Integration

## 1. Overview of the Client Integration Model

Chitra is engineered as a standalone, embeddable 2D rendering library. It enforces a strict separation of concerns between higher-level client UI frameworks (or host applications) and low-level rendering mechanics:

```
+------------------------------------------------------------------------+
|                     LAYER 3: CLIENT UI FRAMEWORK                       |
+------------------------------------------------------------------------+
| - Manages DOM/component hierarchy and widget tree state                |
| - Calculates Flexbox layout, line wraps, and event hit-testing         |
| - Holds pure 32-bit Chitra Handles and assigns 30-bit Draw Orders      |
| - Performs ZERO vertex generation and ZERO direct GPU driver calls     |
+-----------------------------------+------------------------------------+
                                    | Data-Oriented C API calls
+-----------------------------------v------------------------------------+
|                       LAYER 2: CHITRA ENGINE                           |
+------------------------------------------------------------------------+
| - Allocates 32-bit handles in CommandTable (Slot Map)                  |
| - Owns contiguous vertex staging buffers per shader pipeline           |
| - Executes text wrapping, font atlas rasterization, and SDF anti-alias |
| - Re-sorts active entries in microseconds via 64-bit Radix Sort        |
| - Coalesces batches and dispatches minimal draw calls via Sokol RHI    |
+-----------------------------------+------------------------------------+
                                    | Native Hardware Commands
+-----------------------------------v------------------------------------+
|                  LAYER 1 & 0: SOKOL GFX / GPU DRIVER                   |
+------------------------------------------------------------------------+
```

### The Performance Contract

1. **Immediate + Retained Synergy:**
   * **Static Frames:** When no layout or visual properties change, the client performs zero work. Chitra automatically suspends CPU/GPU execution via OS waitable events, achieving true 0% CPU and GPU utilization.
   * **Dynamic Frames:** When a property changes (such as hovering over a button or typing text), the client updates only the affected handle in system RAM and calls `chitra_mark_dirty()`.
2. **Pure Integer Identifiers:**
   * The client assigns visual layering using a 30-bit `Draw Order` integer (0 to 1,073,741,823).
   * Chitra tracks all internal allocations using a plain 32-bit `handle` (`command_id`) indexing directly into `CommandTable`.
3. **Decoupled Text Measurement:**
   * UI layout engines (like Flexbox or grid solvers) require text dimensions before positioning elements. Chitra provides pure CPU text measurement functions (`chitra_measure_text`, `chitra_get_font_metrics`) that evaluate glyph metrics without generating vertices or touching the GPU.

---

## 2. Engine Lifecycle and Runtime Control API

Chitra supports two distinct execution models depending on whether Chitra controls the application event loop or runs embedded inside an existing host application.

### Mode A: Standalone Engine Runner

Used when Chitra owns the OS window and the main event loop via its Hardware Abstraction Layer (HAL).

```c
typedef struct {
    int width;                          // Window width in logical points
    int height;                         // Window height in logical points
    const char* title;                  // Window title string (UTF-8)
    int target_fps;                     // Frame pacing: 0 = VSync; >0 = manual FPS cap
    void (*on_init)(void);              // Called once after GPU context is created
    void (*on_frame)(void);             // Called once per frame when the engine renders
    void (*on_event)(const Chitra_InputEvent* event); // Called when an OS event occurs
    void (*on_cleanup)(void);           // Called before shutdown to free client state
} Chitra_Config;

/* Starts the engine, creates the window, and runs the platform event loop */
void chitra_run(const Chitra_Config* config);

/* Programmatically requests the running chitra_run loop to terminate cleanly */
void chitra_quit(void);
```

### Mode B: Embedded Host Application

Used when an external host application (such as an existing game engine or digital audio workstation) already owns the window and drives its own frame loop.

```c
/* Initializes Chitra internal tables, arenas, and typography subsystems */
void chitra_init(void);

/* Called by the host inside its own render loop before emitting UI commands */
void chitra_begin_frame(int framebuffer_width, int framebuffer_height);

/* Called by the host to execute sorting, batch coalescing, and GPU draw submission */
void chitra_end_frame(void);

/* Shuts down Chitra and frees all memory arenas, staging arrays, and GPU buffers */
void chitra_cleanup(void);
```

### Common Runtime Controls

```c
/* Notifies Chitra that visual state has changed, triggering GPU upload and presentation */
void chitra_mark_dirty(void);

/* Dynamic window controls (valid when running under Mode A) */
void  chitra_set_window_title(const char* title);
void  chitra_get_window_size(int* out_width, int* out_height);
float chitra_get_dpi_scale(void);
```

---

## 3. The Reservation Handle Architecture (Retained Mode)

Instead of forcing developers to supply dozens of parameters up front during element creation, Chitra utilizes a **Reservation-First Architecture**:

1. **Reservation:** The client calls `chitra_create(pipeline)` to reserve a slot in `CommandTable`.
2. **Configuration:** The client configures properties independently (`set_bounds`, `set_color`, `set_content`, `set_wrap`) in any order.
3. **Destruction:** The client calls `chitra_destroy(handle)` when the element is deleted.

```
       Reservation                       Independent Setters                      Destruction
  ┌──────────────────┐           ┌─────────────────────────────────┐          ┌──────────────────┐
  │  chitra_create   │ ──ID───>  │ chitra_rect_set_bounds(id, ...) │  ───>    │  chitra_destroy  │
  │ (pipeline_type)  │           │ chitra_rect_set_color(id, ...)  │          │      (id)        │
  └──────────────────┘           │ chitra_set_draw_order(id, ...)  │          └──────────────────┘
                                 └─────────────────────────────────┘
```

### Pipeline Identification

```c
typedef enum {
    CHITRA_PIPELINE_SHAPE   = 0, // Analytic SDF shapes (rectangles, rounded boxes, pills, borders)
    CHITRA_PIPELINE_TEXT    = 1, // Dynamic typography glyphs sampled from font atlas
    CHITRA_PIPELINE_TEXTURE = 2, // Arbitrary hardware textures, icons, and sprite quads
    CHITRA_PIPELINE_MESH    = 3  // Triangulated 2D vector geometry and SVGs
} Chitra_PipelineType;
```

### Universal Node Functions

These functions operate on any valid 32-bit handle regardless of its underlying pipeline type:

```c
/* Reserves a new 32-bit handle in CommandTable for the specified pipeline */
uint32_t chitra_create(Chitra_PipelineType pipeline);

/* Toggles visibility: sets slot vertex_count = 0 so sorting pass skips it. O(0) cost! */
void chitra_set_visible(uint32_t handle, bool visible);

/* Sets the 30-bit Draw Order integer (0 to 1,073,741,823) in the slot's sort_key */
void chitra_set_draw_order(uint32_t handle, int draw_order);

/* Associates the element with a hardware scissor clipping region (0 to 65,535) */
void chitra_set_clip(uint32_t handle, uint16_t clip_id);

/* Destroys element: records dead hole in staging buffer, pushes ID to free_list */
void chitra_destroy(uint32_t handle);
```

---

## 4. Pipeline Property Setters and Operations

Every setter function is an **$O(1)$ direct memory write** targeting the element's existing staging vertices in system RAM.

### A. The Shape Pipeline (`CHITRA_PIPELINE_SHAPE`)

Operates on 40-byte `Chitra_ShapeVertex` quads rendered via the analytic Signed Distance Field (SDF) shader.

```c
/* Sets position and dimensions in logical window points */
void chitra_rect_set_bounds(uint32_t handle, float x, float y, float w, float h);

/* Sets uniform corner radius in pixels (0.0f = sharp rectangle; >= min(w,h)*0.5f = pill) */
void chitra_rect_set_radius(uint32_t handle, float radius);

/* Sets independent corner radii: Top-Left, Top-Right, Bottom-Right, Bottom-Left */
void chitra_rect_set_radius_corners(uint32_t handle, float tl, float tr, float br, float bl);

/* Sets outer border stroke thickness in pixels (0.0f = no border) */
void chitra_rect_set_border(uint32_t handle, float border_thickness);

/* Sets interior fill color and outline border color as packed RGBA8 integers */
void chitra_rect_set_color(uint32_t handle, uint32_t fill_color, uint32_t border_color);
```

### B. The Text Pipeline (`CHITRA_PIPELINE_TEXT`)

Operates on 20-byte `Chitra_TextVertex` quads. The renderer manages font glyph quad generation and line wrapping directly in its staging arrays.

#### 1. Font Asset Loading
```c
/* Loads a TrueType (.ttf) or OpenType (.otf) font. Returns font_id >= 0 on success */
int chitra_load_font(const char* name, const char* file_path);
```

#### 2. Text Configuration Setters
```c
/* Sets font family and font point size */
void chitra_text_set_font(uint32_t handle, int font_id, float size);

/* Sets baseline top-left starting position in logical points */
void chitra_text_set_pos(uint32_t handle, float x, float y);

/* Updates text string: overwrites in-place if length <= capacity; appends with hole if growing */
void chitra_text_set_content(uint32_t handle, const char* text);

/* Sets packed RGBA8 tint color */
void chitra_text_set_color(uint32_t handle, uint32_t color);

/* Sets additional horizontal tracking space between characters in pixels */
void chitra_text_set_letter_spacing(uint32_t handle, float spacing_px);

/* Word-wrapping: Renderer calculates line breaks within max_width and updates glyph quads */
void chitra_text_set_wrap(uint32_t handle, float max_width);
```

#### 3. Text Layout Queries & Interactive Metrics
```c
/* Queries the calculated bounding box of this text element after wrapping */
void chitra_text_get_bounds(uint32_t handle, float* out_width, float* out_height);

/* Hit-testing: Returns character byte index under coordinate (essential for mouse click cursor) */
int  chitra_text_hit_test(uint32_t handle, float local_x, float local_y);

/* Caret positioning: Returns local (X, Y) pixel coordinate of character index */
void chitra_text_get_char_pos(uint32_t handle, int char_index, float* out_x, float* out_y);

/* Static CPU measurement without allocating a handle (used during layout solve) */
void chitra_measure_text(int font_id, float size, float letter_spacing, 
                         const char* text, float* out_width, float* out_height);

void chitra_measure_text_wrap(int font_id, float size, float letter_spacing, float max_width,
                              const char* text, float* out_width, float* out_height, int* out_line_count);

/* Font vertical metrics for baseline alignment and layout calculations */
void chitra_get_font_metrics(int font_id, float size, 
                             float* out_ascent, float* out_descent, float* out_line_height);
```

### C. The Texture Pipeline (`CHITRA_PIPELINE_TEXTURE`)

Operates on 20-byte `Chitra_TextVertex` quads mapping arbitrary hardware texture images.

#### 1. Texture Asset Management
```c
/* Loads an image file (PNG/JPG) into GPU VRAM. Returns texture_id (0 to 8,191) */
uint16_t chitra_load_texture(const char* file_path);

/* Creates a dynamic GPU texture from raw RGBA8 pixel memory */
uint16_t chitra_create_texture_rgba(int width, int height, const uint8_t* pixels);

/* Updates a sub-region of a dynamic GPU texture */
void chitra_update_texture_rgba(uint16_t texture_id, int x, int y, int w, int h, const uint8_t* pixels);

/* Frees GPU texture and releases descriptor slot */
void chitra_destroy_texture(uint16_t texture_id);
```

#### 2. Textured Node Setters
```c
/* Binds the active texture asset to this node */
void chitra_textured_rect_set_texture(uint32_t handle, uint16_t texture_id);

/* Sets screen destination bounds in logical window points */
void chitra_textured_rect_set_bounds(uint32_t handle, float x, float y, float w, float h);

/* Sets normalized UV coordinates (0.0 to 1.0) for sprite sheets and cropping */
void chitra_textured_rect_set_uv(uint32_t handle, float u0, float v0, float u1, float v1);

/* Sets multiplicative packed RGBA8 tint color (0xFFFFFFFF = untinted original) */
void chitra_textured_rect_set_tint(uint32_t handle, uint32_t tint_color);
```

### D. The Vector & Mesh Pipeline (`CHITRA_PIPELINE_MESH`)

Operates on triangulated 20-byte `Chitra_TextVertex` geometry for SVG paths and custom meshes.

```c
/* Loads an SVG path string or file, triangulates via earcut.h, and assigns to handle */
void chitra_mesh_set_svg(uint32_t handle, const char* svg_path_or_xml, float scale);

/* Sets translation offset in logical points */
void chitra_mesh_set_pos(uint32_t handle, float x, float y);

/* Sets uniform tint color */
void chitra_mesh_set_color(uint32_t handle, uint32_t color);
```

---

## 5. Immediate Mode (Frame-by-Frame Transient Drawing)

For lightweight applications, debugging overlays, or transient frames where retained handles are unnecessary, Chitra provides immediate-mode drawing functions:

```c
/* Clears immediate-mode queues for the active frame (zero heap allocations) */
void chitra_cmd_clear(void);

/* Emits a transient shape quad */
void chitra_draw_rect(int draw_order, float x, float y, float w, float h,
                      float radius, float border, uint32_t fill_color, uint32_t border_color);

/* Emits a transient single-line text string */
void chitra_draw_text(int draw_order, int font_id, float size, float x, float y,
                      const char* text, uint32_t color);

/* Emits a transient textured quad */
void chitra_draw_textured_rect(int draw_order, uint16_t texture_id,
                               float x, float y, float w, float h, uint32_t tint_color);

/* Emits a 2D line divider */
void chitra_draw_line(int draw_order, float x1, float y1, float x2, float y2,
                      float thickness, uint32_t color);
```

---

## 6. Hardware Scissor Clipping API

Chitra maintains a hardware scissor stack supporting up to 65,536 unique clipping boundaries:

```c
/* Pushes a scissor rectangle. Automatically intersects with active parent bounds. Returns clip_id */
uint16_t chitra_push_clip(int x, int y, int w, int h);

/* Pops the most recent scissor rectangle, restoring the parent boundary */
void chitra_pop_clip(void);
```

* **High-DPI Coordinate Translation:** Logical coordinates are automatically scaled to physical framebuffer pixels based on the monitor's display scale factor before programming GPU scissor registers.

---

## 7. The Input and Event System API

Chitra provides a unified, cross-platform OS event abstraction. When running under Standalone Runner mode (`chitra_run`), the Hardware Abstraction Layer captures native platform events (Windows messages, Cocoa events, X11/Wayland signals) and delivers them directly to the client's `config->on_event` callback.

### A. The Input Event Structure

```c
typedef enum {
    CHITRA_EVENT_NONE = 0,
    CHITRA_EVENT_MOUSE_MOVE,
    CHITRA_EVENT_MOUSE_DOWN,
    CHITRA_EVENT_MOUSE_UP,
    CHITRA_EVENT_MOUSE_SCROLL,
    CHITRA_EVENT_KEY_DOWN,
    CHITRA_EVENT_KEY_UP,
    CHITRA_EVENT_CHAR,
    CHITRA_EVENT_RESIZED
} Chitra_EventType;

typedef struct {
    Chitra_EventType type;          // Category of OS input event
    float mouse_x;                  // Mouse horizontal position in logical window points
    float mouse_y;                  // Mouse vertical position in logical window points
    float scroll_x;                 // Horizontal mouse scroll delta
    float scroll_y;                 // Vertical mouse scroll delta
    int button;                     // Mouse button index (0 = Left, 1 = Right, 2 = Middle)
    int key_code;                   // Hardware key code
    uint32_t char_code;             // UTF-32 Unicode codepoint for character typing
    uint32_t modifiers;             // Bitmask of active modifier keys (Shift, Ctrl, Alt, Super)
    int window_width;               // Updated window width in logical points on RESIZED
    int window_height;              // Updated window height in logical points on RESIZED
} Chitra_InputEvent;
```

### B. Modifier Key Flags and Mouse Buttons

```c
/* Mouse button indices */
#define CHITRA_MOUSE_LEFT    0
#define CHITRA_MOUSE_RIGHT   1
#define CHITRA_MOUSE_MIDDLE  2

/* Modifier key bitmasks */
#define CHITRA_MOD_SHIFT     (1 << 0)
#define CHITRA_MOD_CTRL      (1 << 1)
#define CHITRA_MOD_ALT       (1 << 2)
#define CHITRA_MOD_SUPER     (1 << 3)
```

### C. Event Categories and Payload Data

1. **Mouse Movement (`CHITRA_EVENT_MOUSE_MOVE`):**
   * Dispatched when the cursor moves across the window.
   * `mouse_x`, `mouse_y`: Updated logical window coordinates.
   * Client frameworks use these coordinates to hit-test bounding boxes and update hover states.

2. **Mouse Button Events (`CHITRA_EVENT_MOUSE_DOWN`, `CHITRA_EVENT_MOUSE_UP`):**
   * Dispatched on physical mouse clicks or button releases.
   * `button`: Button index (`0 = Left`, `1 = Right`, `2 = Middle`).
   * `modifiers`: State of modifier keys during click.

3. **Mouse Scroll (`CHITRA_EVENT_MOUSE_SCROLL`):**
   * Dispatched on wheel rotation or trackpad swipe gestures.
   * `scroll_x`, `scroll_y`: Directional scroll deltas. Typically consumed by scrollable containers to adjust child offsets.

4. **Keyboard Key Events (`CHITRA_EVENT_KEY_DOWN`, `CHITRA_EVENT_KEY_UP`):**
   * Dispatched on physical key press and release.
   * `key_code`: Platform-independent key code for navigation keys (Arrow keys, Escape, Return, Tab, Backspace).

5. **Text Character Entry (`CHITRA_EVENT_CHAR`):**
   * Dispatched for printable text characters and IME inputs.
   * `char_code`: 32-bit Unicode codepoint. Directly appended to text field buffers without manual keyboard layout decoding.

6. **Window Resize (`CHITRA_EVENT_RESIZED`):**
   * Dispatched when the user resizes the OS window.
   * `window_width`, `window_height`: New window dimensions in logical points.
   * Chitra automatically re-adjusts GPU swapchain targets, recalculates projection matrices, and triggers a multi-frame swapchain presentation countdown.

---

## 8. Complete Client Integration Walkthrough

The following C99 example demonstrates a complete client UI framework integration utilizing the reservation handle pattern, independent property setters, text wrapping, dynamic hover updates, and clean lifecycle management:

```c
#include "chitra.h"
#include <stdio.h>
#include <stdbool.h>

// Client application state and retained handles
static int      g_font_id        = -1;
static uint32_t g_panel_handle   = 0;
static uint32_t g_title_handle   = 0;
static uint32_t g_button_handle  = 0;
static uint32_t g_label_handle   = 0;
static uint32_t g_desc_handle    = 0;

static bool     g_button_hovered = false;
static int      g_click_count    = 0;

// 1. Initialization Hook (Run once after GPU context is ready)
static void client_init(void) {
    g_font_id = chitra_load_font("Inter", "assets/fonts/Inter-Regular.ttf");

    // Reserve handles across pipelines without upfront parameters
    g_panel_handle  = chitra_create(CHITRA_PIPELINE_SHAPE);
    g_button_handle = chitra_create(CHITRA_PIPELINE_SHAPE);
    g_title_handle  = chitra_create(CHITRA_PIPELINE_TEXT);
    g_label_handle  = chitra_create(CHITRA_PIPELINE_TEXT);
    g_desc_handle   = chitra_create(CHITRA_PIPELINE_TEXT);

    // Configure Background Panel
    chitra_set_draw_order(g_panel_handle, 10);
    chitra_rect_set_bounds(g_panel_handle, 40, 40, 720, 520);
    chitra_rect_set_radius(g_panel_handle, 12.0f);
    chitra_rect_set_border(g_panel_handle, 1.5f);
    chitra_rect_set_color(g_panel_handle, 0xFF1E1E2E, 0xFF45475A);

    // Configure Header Title
    chitra_set_draw_order(g_title_handle, 20);
    chitra_text_set_font(g_title_handle, g_font_id, 24.0f);
    chitra_text_set_pos(g_title_handle, 70, 70);
    chitra_text_set_content(g_title_handle, "Chitra Engine Retained UI");
    chitra_text_set_color(g_title_handle, 0xFFFFFFFF);

    // Configure Interactive Button
    chitra_set_draw_order(g_button_handle, 30);
    chitra_rect_set_bounds(g_button_handle, 70, 130, 200, 44);
    chitra_rect_set_radius(g_button_handle, 8.0f);
    chitra_rect_set_border(g_button_handle, 1.0f);
    chitra_rect_set_color(g_button_handle, 0xFF313244, 0xFF89B4FA);

    // Configure Button Label
    chitra_set_draw_order(g_label_handle, 40);
    chitra_text_set_font(g_label_handle, g_font_id, 15.0f);
    chitra_text_set_pos(g_label_handle, 110, 144);
    chitra_text_set_content(g_label_handle, "Clicks: 0");
    chitra_text_set_color(g_label_handle, 0xFFCDD6F4);

    // Configure Multi-Line Wrapped Description
    chitra_set_draw_order(g_desc_handle, 20);
    chitra_text_set_font(g_desc_handle, g_font_id, 14.0f);
    chitra_text_set_pos(g_desc_handle, 70, 210);
    chitra_text_set_content(g_desc_handle, 
        "This text element demonstrates automatic word wrapping handled natively by the Chitra "
        "text pipeline. Each line is converted into contiguous quad streams in system RAM.");
    chitra_text_set_color(g_desc_handle, 0xFFA6ADC8);
    chitra_text_set_wrap(g_desc_handle, 660.0f); // Max wrapping width

    // Initial frame presentation
    chitra_mark_dirty();
}

// 2. Render Hook (Called during active frame presentation)
static void client_frame(void) {
    // Retained elements are already updated in staging arrays.
    // If immediate-mode debug lines or overlays are needed, push them here.
}

// 3. Input Event Hook (Dispatched on OS interaction)
static void client_event(const Chitra_InputEvent* event) {
    if (event->type == CHITRA_EVENT_MOUSE_MOVE) {
        // Hit-test button bounds (70, 130, 200, 44)
        bool hovered = (event->mouse_x >= 70.0f && event->mouse_x <= 270.0f &&
                        event->mouse_y >= 130.0f && event->mouse_y <= 174.0f);

        if (hovered != g_button_hovered) {
            g_button_hovered = hovered;
            uint32_t fill = g_button_hovered ? 0xFF45475A : 0xFF313244;
            
            // O(1) in-place vertex color update directly in shape staging array
            chitra_rect_set_color(g_button_handle, fill, 0xFF89B4FA);
            chitra_mark_dirty();
        }
    } else if (event->type == CHITRA_EVENT_MOUSE_DOWN) {
        if (event->button == 0 && g_button_hovered) {
            g_click_count++;
            char buf[32];
            snprintf(buf, sizeof(buf), "Clicks: %d", g_click_count);

            // In-place text content update (preserves existing memory slot)
            chitra_text_set_content(g_label_handle, buf);
            chitra_mark_dirty();
        }
    }
}

// 4. Teardown Hook (Called on engine shutdown)
static void client_cleanup(void) {
    // Release retained handles cleanly
    chitra_destroy(g_desc_handle);
    chitra_destroy(g_label_handle);
    chitra_destroy(g_title_handle);
    chitra_destroy(g_button_handle);
    chitra_destroy(g_panel_handle);
}

// 5. Application Entry Point
int main(void) {
    Chitra_Config config = {
        .width      = 800,
        .height     = 600,
        .title      = "Chitra Client Integration Demo",
        .target_fps = 0, // VSync enabled (0% idle frame pacing)
        .on_init    = client_init,
        .on_frame   = client_frame,
        .on_event   = client_event,
        .on_cleanup = client_cleanup
    };

    chitra_run(&config);
    return 0;
}
```

---

## 9. Next Steps

To examine the architectural trade-offs, benchmarks, and historical rationale behind the design choices documented in this chapter, continue to:
* **[Chapter 07: Architectural Decision Records (ADRs)](07_architectural_decisions.md)**: Formal records detailing why Chitra uses DOD, reservation handles, dedicated per-pipeline staging arrays, and 30-bit Draw Order sorting keys.
