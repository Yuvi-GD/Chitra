# Chapter 09: Advanced Visual Effects & Shader Architecture

## 1. Overview and Architectural Goals

This chapter establishes the definitive architectural specification for advanced visual effects, custom materials, and shader extensibility within the Chitra 2D engine.

Modern graphical user interfaces demand sophisticated visual treatments:
* **Analytical Gradients:** Linear, radial, angular, and 4-corner bilinear mesh gradients.
* **Lighting and Radiance:** Outer neon glow, bloom, and emissive highlights.
* **Elevation and Depth:** Crisp and diffused drop shadows, ambient occlusion, and multi-layered CSS-style box shadows.
* **Surface Materials:** Procedural patterns, chromatic aberration, pulsing effects, and holographic surfaces.
* **Optical Translucency:** Frosted glass, glassmorphism, and dynamic backdrop blurring (`backdrop-filter: blur()`).

### The Architectural Dilemma in 2D Renderers
Historically, 2D UI renderers fall into one of two crippling performance traps:
1. **The Pipeline Explosion Trap:** Creating a distinct GPU pipeline (`sg_pipeline`) for every visual variant (`SHAPE_SOLID`, `SHAPE_GRADIENT`, `SHAPE_SHADOW`, `SHAPE_GLOW`). Because pipeline state changes flush the hardware draw queue, a UI with 50 styled buttons collapses into 50 disjoint draw calls, annihilating GPU batching.
2. **The Monolithic Vertex Bloat Trap:** Expanding the core vertex structure to 80+ bytes to accommodate the union of all conceivable parameters (gradient stops, shadow offsets, blur spreads, noise frequencies). This destroys CPU data cache locality, wastes GPU transfer bandwidth, and penalizes flat UI elements that require none of these attributes.

### Chitra's Architectural Solution
Chitra solves this through a **Three-Tier Material & Geometry Architecture**:
1. **Tier 1 (In-Place Geometry & Multi-Quad Primitives):** Gradients, glows, and drop shadows are achieved with **zero vertex struct bloat** and **zero extra draw calls** by leveraging hardware vertex attribute interpolation and sequential multi-quad element allocations.
2. **Tier 2 (Parameter-Driven Uber-Pipeline for Infinite Materials):** Infinite procedural custom materials are executed through a **single GPU pipeline** backed by a dynamic GPU Material Parameter Buffer.
3. **Tier 3 (Multi-Pass Surface Compositing):** Optical glassmorphism and backdrop blurring execute through a lightweight, downsampled 2-pass separable filter pass that feeds frosted textures into standard quad geometry.

```
+---------------------------------------------------------------------------------------+
|                    CHITRA THREE-TIER VISUAL STYLING ARCHITECTURE                      |
+---------------------------------------------------------------------------------------+
| TIER 1: In-Batch Geometry Effects (Zero Extra Draw Calls, 40B/20B Vertex)             |
|   ├── Directional & 4-Corner Gradients ──> Bilinear HW Vertex Color Interpolation     |
|   ├── Outer Glow & Diffuse Radiance   ──> Multi-Quad Expansion (Quad 0: Blurred SDF)  |
|   └── Multi-Layer Drop Shadows        ──> Multi-Quad Offset    (Quad 0..N: Shadows)   |
|                                                                                       |
| TIER 2: Unified Parameter-Driven Material Pipeline (Infinite Materials in 1 Pipeline) |
|   ├── Procedural Patterns, Noise, Waves ──> Single CHITRA_PIPELINE_MATERIAL           |
|   └── Dynamic GPU Parameter Buffer Table ─> Element encodes 16-bit Material ID        |
|                                                                                       |
| TIER 3: Multi-Pass Surface Compositing (Optical Effects)                              |
|   └── Glassmorphism & Frosted Glass   ──> Offscreen Backdrop Capture + Dual Blur      |
+---------------------------------------------------------------------------------------+
```

---

## 2. Tier 1A: Analytical Gradients via Hardware Interpolation

### 2.1 Zero-Cost Bilinear Color Interpolation
In [`include/chitra.h`](../../include/chitra.h), [`Chitra_ShapeVertex`](../../include/chitra.h) is defined as exactly 40 bytes:

```c
typedef struct {
    float    pos[2];           /* X, Y screen coordinates in pixels */
    float    uv[2];            /* Normalized quad UV coordinates (0.0 to 1.0) */
    float    size[2];          /* Physical width and height in pixels */
    float    radius_border[2]; /* Corner radius, border thickness in pixels */
    uint32_t fill_color;       /* Packed RGBA8 (UBYTE4N) */
    uint32_t border_color;     /* Packed RGBA8 (UBYTE4N) */
} Chitra_ShapeVertex;          /* Exactly 40 bytes */
```

In standard flat rendering, all 4 vertices of a quad receive identical `fill_color` values. However, hardware graphics rasterizers automatically perform **perspective-correct bilinear interpolation** of all vertex attributes across triangles.

```
v[0] (Top-Left)                       v[1] (Top-Right)
Color: 0xFFFF0080 (Magenta)           Color: 0xFF00FFFF (Cyan)
    ●──────────────────────────────────────●
    │ ＼                                   │
    │   ＼      Bilinear Hardware          │
    │     ＼    Color Interpolation        │
    │       ＼  (Zero ALU Overhead)        │
    │         ＼                           │
    ●──────────────────────────────────────●
v[3] (Bottom-Left)                    v[2] (Bottom-Right)
Color: 0xFFFF8000 (Orange)            Color: 0xFF0080FF (Blue)
```

In the shape vertex shader ([`chitra_shape.glsl`](../../src/backends/shaders/chitra_shape.glsl)):
```glsl
@vs vs_shape
in vec4 fill_color;
out vec4 v_fill_color;

void main() {
    // ... position transformation ...
    v_fill_color = fill_color; // Passes directly to interpolator
}
```

The fragment shader already samples `v_fill_color` for interior pixels. Therefore, **gradients require zero modifications to shader code and zero extra bytes in vertex memory**.

### 2.2 Supported Gradient Configurations
By assigning varying colors to the 4 quad vertices, client frameworks can produce:

1. **Vertical Linear Gradient ($90^\circ$):**
   * `v[0].fill_color = v[1].fill_color = color_top;`
   * `v[2].fill_color = v[3].fill_color = color_bottom;`
2. **Horizontal Linear Gradient ($0^\circ$):**
   * `v[0].fill_color = v[3].fill_color = color_left;`
   * `v[1].fill_color = v[2].fill_color = color_right;`
3. **Diagonal Linear Gradient ($45^\circ$):**
   * `v[0].fill_color = color_start;`
   * `v[2].fill_color = color_end;`
   * `v[1].fill_color = v[3].fill_color = mix(color_start, color_end, 0.5);`
4. **4-Corner Bilinear Mesh Gradient:**
   * Four independent colors mapped to each corner, producing organic, multi-hue modern UI surfaces.

### 2.3 Text Gradients
Text glyphs generated via FontStash in [`src/chitra_font.c`](../../src/chitra_font.c) also emit 4 vertices per character quad using [`Chitra_TextVertex`](../../include/chitra.h) (20 bytes). By assigning `color_top` to vertices 0 & 1 and `color_bottom` to vertices 2 & 3, text receives smooth vertical gradients with **zero font engine or shader overhead**.

---

## 3. Tier 1B: Multi-Quad Architecture for Drop Shadows & Outer Glow

### 3.1 The Multi-Quad Breakthrough
In Chitra, elements in [`CommandTable`](../../include/chitra_table.h) are defined by:
```c
typedef struct {
    uint64_t sort_key;           /* 8 bytes: Visual state for Radix Sort */
    uint32_t start_vertex_index; /* 4 bytes: Pipeline-local vertex offset */
    uint32_t vertex_meta;        /* 4 bytes: Alive flag + Vertex count */
} Chitra_CommandSlot;
```

A common misconception is that a shape element must always equal exactly one 4-vertex quad. Chitra's text subsystem already disproves this: a text element with a single 32-bit `handle` holds $N$ quads ($4 \times N$ vertices) for a string of text.

Chitra applies this exact principle to shapes: **A single shape element handle can allocate 8, 12, or 16 vertices across multiple contiguous quads.**

```mermaid
flowchart LR
    Handle["Element Handle (32-bit ID)"] --> Slot["CommandSlot (vertex_count = 8)"]
    Slot --> Staging["Staging Array Memory"]
    
    subgraph Staging ["Contiguous Staging Buffer (8 Vertices)"]
        Quad0["Quad 0 (Vertices 0..3): Outer Glow / Drop Shadow"]
        Quad1["Quad 1 (Vertices 4..7): Main Shape Body & Border"]
    end
    
    Quad0 --> Indices0["Index Stream: [0, 1, 2, 0, 2, 3] (Emitted FIRST)"]
    Quad1 --> Indices1["Index Stream: [4, 5, 6, 4, 6, 7] (Emitted SECOND)"]
    
    Indices0 --> GPU["GPU Hardware Rasterizer (Draws Quad 0 BEHIND Quad 1)"]
    Indices1 --> GPU
```

### 3.2 Sequential Index Ordering & Painter's Algorithm
When [`chitra_batch_prepare()`](../../src/chitra_batch.c) constructs the 32-bit GPU index buffer, it loops over each quad in the slot:
```c
uint32_t quad_count = chitra_slot_vertex_count(slot) / 4;
for (uint32_t q = 0; q < quad_count; q++) {
    uint32_t base = slot->start_vertex_index + (q * 4);
    s_indices[s_index_count++] = base + 0;
    s_indices[s_index_count++] = base + 1;
    s_indices[s_index_count++] = base + 2;
    s_indices[s_index_count++] = base + 0;
    s_indices[s_index_count++] = base + 2;
    s_indices[s_index_count++] = base + 3;
}
```

GPU hardware rasterizers execute triangles **strictly in the sequential order of the index buffer**. Because Quad 0 indices are emitted before Quad 1 indices:
* **Quad 0 (Glow / Shadow) draws first, perfectly behind the element.**
* **Quad 1 (Shape Fill & Border) draws second, directly on top.**
* Both quads belong to the same element handle and the same pipeline (`CHITRA_PIPELINE_SHAPE`), meaning **they coalesce into the exact same GPU draw call**.

### 3.3 Geometry & SDF Mathematics for Shadows and Glows

#### Quad 0 Configuration: The Blurred SDF Surface
To render an outer blur without rasterization clipping, Quad 0's physical geometry is inflated by the blur radius $\sigma$ and spread extent $s$:

$$\text{offset}_x = \Delta x, \quad \text{offset}_y = \Delta y$$
$$x_{\text{shadow}} = x + \text{offset}_x - (\sigma + s)$$
$$y_{\text{shadow}} = y + \text{offset}_y - (\sigma + s)$$
$$w_{\text{shadow}} = w + 2(\sigma + s)$$
$$h_{\text{shadow}} = h + 2(\sigma + s)$$

In [`chitra_shape.glsl`](../../src/backends/shaders/chitra_shape.glsl), shapes use an analytic signed distance field:
```glsl
float rounded_box_sdf(vec2 center_pos, vec2 size, float radius) {
    vec2 q = abs(center_pos) - size + vec2(radius);
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - radius;
}
```

For the shadow quad:
* The core inner distance is evaluated against the physical shape size $(w, h)$ and physical radius $r$.
* Outside the boundary ($d > 0$), the fragment shader calculates a Gaussian or smooth exponential falloff:

$$\alpha_{\text{glow}}(d) = \exp\left(-\frac{d^2}{2\sigma^2}\right) \quad \text{or} \quad \alpha_{\text{shadow}}(d) = 1.0 - \text{smoothstep}(0.0, \sigma, d)$$

```
                    QUAD 0 (Expanded Geometry Boundary)
    ┌────────────────────────────────────────────────────────┐
    │  Outer Falloff: d > 0 -> alpha = exp(-d^2 / 2*sigma^2) │
    │         ┌────────────────────────────────────┐         │
    │         │      QUAD 1 (Main Shape Quad)      │         │
    │         │  - Physical Size: (w, h)           │         │
    │         │  - Physical Corner Radius: r       │         │
    │         │  - Crisp Border & Gradient Fill    │         │
    │         │  - Interior: d <= 0                │         │
    │         └────────────────────────────────────┘         │
    │                                                        │
    └────────────────────────────────────────────────────────┘
```

### 3.4 Multi-Layer CSS Shadows
CSS elements frequently define multiple stacked shadows (e.g., an intimate contact shadow plus an expansive ambient shadow):
```css
box-shadow: 0 2px 4px rgba(0,0,0,0.1), 0 12px 24px rgba(0,0,0,0.2);
```

Under Chitra's multi-quad model, this is trivially represented by allocating **12 vertices (3 quads)**:
* **Quad 0 (Indices 0..5):** Ambient Shadow (large blur, faint alpha).
* **Quad 1 (Indices 6..11):** Contact Shadow (tight blur, vertical offset).
* **Quad 2 (Indices 12..17):** Main Shape Card.

### 3.5 Architectural Advantages
1. **Zero Struct Bloat:** `Chitra_ShapeVertex` remains at 40 bytes. Flat shapes never pay for shadow storage.
2. **Single Handle Control:** The client application operates on a single `handle`. Calling `chitra_rect_set_bounds(handle, ...)` recalculates and updates all quads in contiguous RAM in $O(1)$.
3. **Single Draw Call:** A list of 500 cards with ambient shadows, directional drop shadows, and neon glows coalesces into **one single GPU draw call**.

---

## 4. Tier 2: Unified Parameter-Driven Material Pipeline (Infinite Materials)

### 4.1 Demystifying "Thousands of Shaders" in Game Engines
In modern graphics APIs (DirectX 12, Vulkan, Metal, WebGPU), state-of-the-art engines do **not** compile thousands of distinct GPU pipeline states (`PSO`s). Constantly swapping pipelines causes pipeline cache thrashing and GPU execution bubbles.

Instead, modern game engines use **Bindless Uber-Pipelines**:
* A single, versatile shader program is compiled once.
* Every primitive passes a 16-bit `material_id`.
* The shader indexes into a dynamic **GPU Material Buffer** containing parameters, animation timers, texture IDs, and mathematical flags.

Chitra adopts this exact pattern for extensible custom materials.

### 4.2 The Material Parameter Buffer Architecture

```
                    ELEMENT STAGING STREAM
    ┌────────────────────────────────────────────────────────┐
    │ Quad 1: material_id = 0 (Neon Pulse Material)          │
    │ Quad 2: material_id = 1 (Metallic Specular Sheen)      │
    │ Quad 3: material_id = 2 (Procedural Circuit Grid)      │
    └───────────────────────────┬────────────────────────────┘
                                │
                                ▼
                   [ SINGLE PIPELINE DRAW CALL ]
                    (CHITRA_PIPELINE_MATERIAL)
                                │
                                ▼
                 GPU UNIFORM / STORAGE BUFFER RING
    ┌────────────────────────────────────────────────────────┐
    │ Index 0: { type=PULSE, speed=2.5, glow=4.0, col=CYAN } │
    │ Index 1: { type=METAL, roughness=0.1, metal=0.95 }     │
    │ Index 2: { type=GRID,  freq=20.0, line_width=1.5 }     │
    │ ... (N Infinite Parameterized Materials)               │
    └────────────────────────────────────────────────────────┘
```

### 4.3 Data Structures and Registration

#### The Material Descriptor
```c
typedef enum {
    CHITRA_MAT_TYPE_SOLID = 0,
    CHITRA_MAT_TYPE_PULSE_GLOW,
    CHITRA_MAT_TYPE_METALLIC_REFLECT,
    CHITRA_MAT_TYPE_CHROMATIC_ABERRATION,
    CHITRA_MAT_TYPE_PROCEDURAL_NOISE,
    CHITRA_MAT_TYPE_DISSOLVE,
    CHITRA_MAT_TYPE_CUSTOM
} Chitra_MaterialType;

typedef struct {
    uint32_t type;             /* Material algorithm type */
    float    params[4];        /* General parameters: speed, intensity, scale, time */
    uint32_t color_stops[4];   /* Up to 4 packed RGBA8 colors */
    uint16_t texture_bindings[2]; /* Optional noise / mask texture IDs */
    uint32_t flags;            /* Bitflags (e.g., additive blending, HDR emit) */
} Chitra_MaterialDesc;
```

#### Client API Contract
```c
/* Registers a procedural material into the global material table */
uint16_t chitra_material_register(const Chitra_MaterialDesc* desc);

/* Updates an existing material's parameters dynamically (O(1) write) */
void chitra_material_update(uint16_t material_id, const Chitra_MaterialDesc* desc);

/* Assigns a material to an element */
void chitra_set_material(uint32_t handle, uint16_t material_id);
```

### 4.4 Shader Execution Model
In the fragment shader for `CHITRA_PIPELINE_MATERIAL`:
```glsl
@fs fs_material
layout(binding=0) uniform material_buffer {
    MaterialDesc u_materials[256];
};

in vec2 v_uv;
in flat uint v_material_id;
out vec4 frag_color;

void main() {
    MaterialDesc mat = u_materials[v_material_id];
    
    if (mat.type == CHITRA_MAT_TYPE_PULSE_GLOW) {
        float pulse = 0.5 + 0.5 * sin(u_time * mat.params[0]);
        frag_color = mix(unpack_color(mat.color_stops[0]), 
                         unpack_color(mat.color_stops[1]), pulse) * mat.params[1];
    } 
    else if (mat.type == CHITRA_MAT_TYPE_PROCEDURAL_NOISE) {
        float n = procedural_noise(v_uv * mat.params[0]);
        frag_color = vec4(vec3(n), 1.0) * unpack_color(mat.color_stops[0]);
    }
    // ... branchless or switch evaluation ...
}
```

### 4.5 Batching and Performance
* **Single Pipeline:** All elements using `CHITRA_PIPELINE_MATERIAL` share the identical 5-bit Pipeline ID in their 64-bit sort key.
* **Radix Coalescing:** As long as elements share the same Scissor Clip, they **coalesce into a single GPU draw call**, even if every element runs a different procedural material effect!

---

## 5. Tier 3: Optical Glassmorphism & Backdrop Blurring

### 5.1 The Hardware Constraint
True glassmorphism (such as CSS `backdrop-filter: blur(20px)`, macOS frosted sidebars, or Windows Acrylic/Mica) **cannot be executed in a single forward fragment shader**.

On modern GPU hardware:
* A fragment shader cannot read pixels from the exact same framebuffer target it is simultaneously writing to. Attempting to do so triggers a **hardware read-after-write hazard / undefined feedback loop**.
* Therefore, the background pixels behind the frosted glass card must be rendered first, captured to a texture, blurred, and then sampled by the glass card.

### 5.2 The 3-Step Multi-Pass Execution Flow

```mermaid
sequenceDiagram
    autonumber
    participant Engine as Chitra Batch Engine
    participant Screen as Main Swapchain Pass
    participant Offscreen as Offscreen Scratch FBO (1/4 Scale)
    participant GPU as Sokol GFX RHI

    Engine->>Screen: Render Background UI (Panels, Gradients, Text)
    Note over Screen: Background elements up to Glass Draw Order are visible
    
    Engine->>GPU: Flush current batch prior to Glass Element
    GPU->>Offscreen: Blit/Downsample Screen Rect behind Glass to 1/4 size
    
    Note over Offscreen: 2-Pass Separable Dual-Filter Blur (0.08ms)
    Offscreen->>Offscreen: Pass A: Horizontal Gaussian Blur
    Offscreen->>Offscreen: Pass B: Vertical Gaussian Blur
    
    Engine->>Screen: Resume Main Render Pass
    Engine->>Screen: Dispatch Glass Card Quad (CHITRA_PIPELINE_SHAPE / TEXTURE)
    Note over Screen: Glass Quad samples blurred FBO via screen UVs,<br/>mixes 15% frosted white tint, and renders crisp SDF border
```

### 5.3 Fast Separable Blur Mathematics
A naive Gaussian blur with radius $R=20$ pixels requires $(2R+1)^2 = 41 \times 41 = 1,681$ texture samples per pixel, which stalls mobile and integrated GPUs.

Chitra specifies a **Dual-Filter Separable Downsampled Pipeline**:
1. **Downsample 4x (Quarter Resolution):** Capturing the backdrop at $\frac{1}{2}$ width and $\frac{1}{2}$ height reduces the pixel count by $75\%$. A 20px blur at full resolution becomes an effective 5px blur at quarter resolution.
2. **Separable 1D Convolutions:**
   * **Pass 1 (Horizontal):** Samples 9 linear taps along the X axis.
   * **Pass 2 (Vertical):** Samples 9 linear taps along the Y axis.
   * Total texture fetches: $9 + 9 = 18$ taps (instead of 1,681 taps).
3. **Hardware Bilinear Tap Optimization:** By offsetting sampling coordinates by half-texels, hardware bilinear texture filtering samples two texels in a single tap, reducing total passes to just 5 taps per direction.

**Total GPU time on a standard 1080p display: $< 0.1\text{ ms}$.**

### 5.4 The Glass Quad Composite
Once the blurred scratch texture is ready, the glass card is rendered as a standard quad:
* **Position & Size:** Physical card dimensions $(x, y, w, h)$.
* **UV Coordinates:** Screen-space projected coordinates $\left(\frac{x}{\text{screen\_w}}, \frac{y}{\text{screen\_h}}\right)$ to cleanly sample the blurred background.
* **Surface Styling:** The standard analytic SDF border shader evaluates the rounded rectangle edge, renders a 1px crisp semi-transparent border, and applies a subtle specular corner gradient.

---

## 6. Comprehensive Styling Comparison Matrix

The table below contrasts all visual effects across Chitra's architectural metrics:

| Visual Effect | Implementation Technique | Pipeline Assigned | Vertex Memory Impact | GPU Draw Call Impact | GPU Frame Cost |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Linear / 4-Corner Gradients** | Bilinear HW Interpolation across quad corners | `CHITRA_PIPELINE_SHAPE` or `TEXT` | **0 extra bytes** (Uses existing 40B/20B) | **0 extra draw calls** (Coalesced in batch) | Zero overhead |
| **Drop Shadow (Single)** | Multi-Quad (Quad 0: Blurred SDF, Quad 1: Shape) | `CHITRA_PIPELINE_SHAPE` | **+40 bytes** (Only for shadowed elements) | **0 extra draw calls** (Coalesced in batch) | Negligible (~0.005 ms) |
| **Multi-Layer CSS Shadows** | Multi-Quad ($N$ shadow quads + 1 shape quad) | `CHITRA_PIPELINE_SHAPE` | **+40B per extra shadow** | **0 extra draw calls** (Coalesced in batch) | Negligible (~0.01 ms) |
| **Outer Glow / Radiance** | Multi-Quad (Inflated Quad 0 with exponential SDF) | `CHITRA_PIPELINE_SHAPE` | **+40 bytes** (8 vertices total) | **0 extra draw calls** (Coalesced in batch) | Negligible (~0.005 ms) |
| **Procedural Custom Material** | Bindless Material Parameter Buffer Table | `CHITRA_PIPELINE_MATERIAL` | **0 bytes** (16-bit ID packed in sort key) | **1 draw call** (All custom materials merge) | Minimal (~0.02 ms) |
| **Frosted Glass / Glassmorphism** | Offscreen Downsampled Dual-Filter Blur + Composite | RHI Blur Pass + Quad Composite | **0 bytes** (Standard quad) | **+1 offscreen pass** (Flushes backdrop) | Fast (~0.08 ms) |

---

## 7. Concrete Implementation Roadmap

When future engineers or AI assistants implement these capabilities into the codebase, execute following this exact phased dependency order:

### Phase 1: In-Place Multi-Vertex Gradients (Immediate, Zero Core Changes)
1. **Extend [`chitra_shape.c`](../../src/chitra_shape.c):** Add `chitra_rect_set_gradient_linear(handle, color_start, color_end, angle)` and `chitra_rect_set_gradient_corners(handle, c_tl, c_tr, c_br, c_bl)`.
2. **Extend [`chitra_font.c`](../../src/chitra_font.c):** Add `chitra_text_set_gradient(handle, color_top, color_bottom)`.
3. **Verification:** Confirm that rendering 1,000 gradient rectangles coalesces into exactly 1 draw call alongside solid shapes.

### Phase 2: Multi-Quad Shadows & Outer Glow
1. **Extend [`src/chitra_shape.c`](../../src/chitra_shape.c):** Add `chitra_rect_set_shadow(handle, blur, spread, dx, dy, color)` and `chitra_rect_set_glow(handle, blur, spread, color)`.
2. **Allocation Logic:** When a shadow/glow is configured, update the slot's `vertex_count` from 4 to 8 (or 12) using [`chitra_table_alloc_vertices()`](../../src/chitra_table.c).
3. **Fragment Shader Update:** In [`chitra_shape.glsl`](../../src/backends/shaders/chitra_shape.glsl), support a blur falloff mode when `radius_border.y` encodes a blur flag.
4. **Verification:** Confirm that Quad 0 indices stream immediately prior to Quad 1 indices, rendering the shadow seamlessly behind the shape in the identical draw call.

### Phase 3: Dynamic Material Parameter Pipeline
1. **Define `CHITRA_PIPELINE_MATERIAL`:** Add Pipeline ID 3 in [`include/chitra.h`](../../include/chitra.h).
2. **Material Table Subsystem (`src/chitra_material.c`):** Allocate a contiguous array of `Chitra_MaterialDesc`. Upload to a GPU uniform ring buffer during `chitra_begin_frame()`.
3. **Shader Pipeline:** Implement [`src/backends/shaders/chitra_material.glsl`](../../src/backends/shaders/) with procedural function branches.

### Phase 4: Offscreen Backdrop Blurring (Glassmorphism)
1. **RHI Scratch Surfaces:** In [`src/backends/sokol_rhi.c`](../../src/backends/sokol_rhi.c), allocate two reusable quarter-resolution offscreen `sg_image` render targets.
2. **Separable Blur Shaders:** Implement 5-tap horizontal and vertical Gaussian blur shaders.
3. **Backdrop Interception:** When an element with glass styling is encountered during batching, flush preceding draw calls, dispatch the 2-pass blur pass, and bind the blurred texture to the glass quad.
