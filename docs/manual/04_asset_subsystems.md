# Chapter 04: Asset Subsystems (Fonts, Textures, and Vectors)

## 1. Overview of Asset Management in Chitra

A rendering engine must handle three primary categories of visual assets beyond simple geometric shapes:
1. **Typography:** Rasterizing font glyphs, managing texture atlases, shaping text runs, and computing layout bounds.
2. **Dynamic RGBA Textures:** Loading image files (PNG, JPG, BMP), dynamic VRAM atlas bin-packing, standalone GPU allocations, and spritesheet sub-rect mapping.
3. **Vector Graphics:** Ingesting arbitrary vector paths, polygons, and SVG curves without introducing heavy runtime dependencies.

Chitra decouples asset ingestion from vertex submission. Regardless of whether an asset originates from a TrueType font file, a raster image, or an SVG path, it is processed into standard 20-byte vertices and submitted through unified GPU pipelines.

```
+----------------------------------------------------------------------------+
|                          ASSET INGESTION LAYER                             |
+-------------------+------------------------------+-------------------------+
| Typography        | Dynamic RGBA Textures        | Vector Subsystem        |
| - FontStash (Lean)| - stb_image (PNG/JPG/BMP)    | - SVG Path Parsing      |
| - FreeType/HarfB. | - stb_rect_pack (VRAM Atlas) | - Contour Extraction    |
+-------------------+------------------------------+-------------------------+
                                     |
                                     v
+----------------------------------------------------------------------------+
|                      PROCESSING & PACKING STAGE                            |
| - Dynamic Alpha Font Atlas Packing | - Direct-to-VRAM Atlas Sub-Upload     |
| - Zero CPU Staging for Images      | - earcut.h Triangulation              |
+------------------------------------+---------------------------------------+
                                     |
                                     v
+----------------------------------------------------------------------------+
|                    UNIFIED CHITRA PIPELINE EMISSION                        |
| - Staging Array: Contiguous 20-byte Chitra_TextVertex quads & meshes       |
| - GPU Shaders: Texture/Text/Mesh Pipeline (Pure modulation, zero branching)|
+----------------------------------------------------------------------------+
```

---

## 2. The Modular Typography Subsystem

Text rendering is fundamentally modular in Chitra. The engine defines a strict interface for font loading, measurement, and glyph quad emission, allowing different font backends to be plugged in based on application requirements.

### Pluggable Font Backends

#### Backend A: Lightweight Embedded (FontStash + stb_truetype)
* **Target:** Applications requiring minimal binary size, instant initialization, and zero external build dependencies.
* **Architecture:** Uses a single-header implementation (`fontstash.h` backed by `stb_truetype.h`). Fonts are loaded directly from memory or disk, and individual glyphs are rasterized on demand into an 8-bit grayscale texture atlas.
* **Footprint:** Less than 200 KB of code size and approximately 1 MB of VRAM for a 1024x1024 texture atlas.

#### Backend B: Enterprise Typography (FreeType + HarfBuzz)
* **Target:** Applications requiring internationalization, complex non-Latin scripts, and advanced typographic features.
* **Architecture:** 
  * **HarfBuzz:** Receives full UTF-8 strings and executes complex script shaping, handling Arabic ligatures, Indic conjuncts, bidirectional text (RTL/LTR), and emoji sequences.
  * **FreeType:** Takes shaped glyph IDs and rasterizes them into the exact same texture atlas format used by FontStash.
  * **Text Outlines:** Utilizes FreeType's `FT_Stroker` to rasterize outline glyphs directly into the atlas. Outline quads are drawn behind fill quads using the exact same shader pipeline.

### The Unified Atlas Guarantee
Regardless of which font engine generates the glyphs:
1. Glyphs are rasterized into an 8-bit alpha texture atlas.
2. Output geometry is emitted as standard 20-byte `Chitra_TextVertex` quads.
3. The GPU shader (`chitra_text.glsl`), vertex staging arrays, and 64-bit sort key mechanics remain completely unchanged.

---

## 3. Dynamic Texture Atlas Management

Chitra maintains font glyphs in a dynamic texture atlas (typically 1024x1024 pixels in 8-bit grayscale format).

```
+------------------------------------------------------------------------+
|                     DYNAMIC FONT ATLAS (1024x1024)                     |
+------------------------------------------------------------------------+
| [A][B][C][D][E][F][G][H] ... Cached glyphs (Already in VRAM)           |
|                                                                        |
| [New glyph: 'W'][New glyph: 'g'] <--- Dirty Region [x0, y0, x1, y1]    |
| (Rasterized during current frame on CPU)                               |
|                                                                        |
| Empty Atlas Space                                                      |
+------------------------------------------------------------------------+
```

### The Single-Upload Rule
In modern graphics APIs, calling texture update functions multiple times within a single frame degrades performance and risks exhausting driver staging rings.

Chitra enforces a strict single-upload policy:
1. **CPU Accumulation:** During widget creation or text updates, glyphs are rasterized into Chitra's system RAM bitmap. Each new glyph expands an internal dirty bounding box: `[dirty_x0, dirty_y0, dirty_x1, dirty_y1]`.
2. **Deferred Synchronization:** Chitra performs **zero GPU texture uploads** while the client application is issuing text commands.
3. **Single GPU Upload:** At the very end of frame processing, right before draw commands are dispatched to the GPU, Chitra checks whether any glyphs were added. If the atlas is dirty, it executes **exactly one** `update_image()` call covering the bounding dirty rectangle.

---

## 4. The Dynamic RGBA Texture Subsystem

Beyond typography, UI applications render rich visual assets: icons, avatars, photos, backgrounds, and game sprites. In naive 2D engines, every loaded image allocates an isolated GPU texture, causing severe draw call fragmentation whenever adjacent elements sample different textures.

Chitra implements a **Hybrid Direct-to-VRAM Texture Subsystem** backed by [`stb_image.h`](../../third_party/stb/stb_image.h) and [`stb_rect_pack.h`](../../third_party/stb/stb_rect_pack.h):

```
                       INCOMING IMAGE ASSET
                                │
                  Dimension Threshold: 512x512
                                │
        ┌───────────────────────┴───────────────────────┐
        ▼                                               ▼
 Width/Height > 512px                           Width/Height <= 512px
(Large Photos, Backgrounds)                    (Icons, Badges, Avatars, Buttons)
        │                                               │
        ▼                                               ▼
[ Standalone GPU Texture ]                    [ Dynamic 1024x1024 RGBA Atlas ]
 - Isolated GPU handle                         - Packed via stb_rect_pack
 - UVs normalized: (0..1)                      - Sub-rect UVs assigned to quad
 - Zero CPU staging RAM                        - Coalesces into 1 GPU Draw Call!
```

### 4.1 Zero-Byte CPU Staging (Direct-to-VRAM Streaming)
Traditional engines keep multi-megabyte image copies in CPU system RAM so they can re-upload them or perform CPU blitting. Chitra enforces a **Zero CPU Staging Policy**:
1. When [`chitra_texture_load()`](../../src/chitra_texture.c) or [`chitra_texture_load_file()`](../../src/chitra_texture.c) is called, pixels are uploaded immediately to GPU VRAM via `rhi->update_texture_sub_rgba()`.
2. The source CPU pixel buffer is freed instantaneously.
3. The element's quad vertices receive normalized UVs addressing the exact packed region within the atlas page.

### 4.2 Multi-Page Atlas Growth & Auto-Spillover
A single atlas page is configured to 1024x1024 pixels (or 2048x2048). When an incoming icon cannot fit within the current active atlas:
1. `stbrp_pack_rects()` detects the page is full.
2. Chitra dynamically requests a new RGBA texture handle from the RHI.
3. The new page becomes the active packing cursor; subsequent elements pack into Page 2 without invalidating Page 1.

### 4.3 Ref-Counting, Dead Pixels & 35% Auto-Evacuation
To prevent VRAM memory leaks in long-running applications:
* Every texture tracks `ref_count` (number of living quads sampling it), `allocated_area`, and `dead_area`.
* When a textured element is destroyed via `chitra_destroy(handle)`, `chitra_texture_on_destroy()` decrements `ref_count` and adds the quad's pixel area to `dead_area`.
* When an atlas page's dead ratio exceeds **35%** ($\frac{\text{dead\_area}}{\text{allocated\_area}} \ge 0.35$):
  * The page is marked for evacuation.
  * Once living references reach zero, the entire GPU texture is destroyed and recycled via the RHI free list.

### 4.4 Spritesheet & Local Rect UV Mapping
For game sprites, icon sheets, and UI texture atlases, [`chitra_texture_set_src_rect()`](../../src/chitra_texture.c) allows clients to specify sub-rectangles `(src_x, src_y, src_w, src_h)` relative to original image dimensions:
* **Standalone Textures:** Directly maps proportional UV ranges within $(0.0 \dots 1.0)$.
* **Atlas-Packed Textures:** Seamlessly translates the local crop relative to the allocated bounding box within the larger atlas page.
* The client framework uses the exact same function signature regardless of where the image resides.

---

## 5. Decoupled Text Measurement and Word Wrapping

A common architectural trap in UI rendering is coupling text measurement with geometry generation. Layout engines (such as Flexbox or CSS solvers) must know the rendered dimensions of a text block *before* calculating parent container sizes and child positions.

Chitra provides a zero-allocation measurement API:
* **Function:** `chitra_measure_text(font_id, font_size, wrap_width, text, &out_width, &out_height)`
* **Behavior:** It measures glyph advances, evaluates word boundaries, and computes line breaks without allocating memory or emitting vertices.
* **Layout Workflow:** The client calls `chitra_measure_text()` during its layout pass to resolve widget dimensions, then calls `chitra_create_text()` or `chitra_update_text()` with the finalized screen coordinates.

### Word-Boundary Wrapping Algorithm
When `wrap_width > 0`, text wrapping operates on whitespace word boundaries:
1. **Advance Accumulation:** The cursor accumulates glyph advance widths (`cursor_x += advance`).
2. **Boundary Lookahead:** When a word would exceed `wrap_width`, the cursor resets to the line start (`cursor_x = start_x`) and advances vertically by the font line height (`cursor_y += line_height`).
3. **Soft vs. Hard Breaks:** Explicit newline characters (`\n`) force immediate vertical line advances, resetting `cursor_x`.
4. **Bounding Calculation:** The total rendered width and height are recorded and returned to the caller.

---

## 6. The Vector Graphics Subsystem (SVG and Polygons)

Many UI toolkits embed massive C++ vector rasterizers (such as Skia, Cairo, or ThorVG) to display SVG icons and vector illustrations. These libraries add megabytes of binary bloat and consume significant CPU cycles rasterizing paths into software bitmaps.

Chitra utilizes a lightweight, hardware-accelerated vector pipeline:

```
[ SVG Path Data ]
       │
       ▼
[ Contour Decomposition (Bézier Subdivision / Polygon Extraction) ]
       │
       ▼
[ Zero-Allocation Triangulation via earcut.h ]
       │
       ▼
[ Flat 2D Triangle Stream (20-byte vertices) ]
       │
       ▼
[ Chitra Mesh Pipeline -> GPU Hardware Rasterizer ]
```

### Triangulation via earcut.h
1. **Path Decomposition:** SVG path strings are parsed into sequences of 2D polygon contours. Curved Bézier segments are subdivided into linear segments based on display scale.
2. **Triangulation:** Contours are processed using `earcut.h`, a tiny, zero-allocation C polygon triangulation library. It converts complex concave polygons, shapes with holes, and path outlines into indexable 2D triangles.
3. **Hardware Emission:** The resulting vertices are emitted directly into Chitra's 20-byte mesh pipeline as flat-colored or gradient triangles.
4. **Hardware Acceleration:** The GPU rasterizes these triangles natively at full monitor refresh rates. Zooming and scaling simply require re-triangulating the path without texture blurring.

---

## 7. High-DPI Displays and Scissor Coordinate Mapping

Modern desktop environments frequently operate under display scaling factors (e.g., 1.25x, 1.5x, 2.0x on Retina or 4K monitors). This introduces a critical distinction between **logical UI coordinates** and **physical framebuffer pixels**:

### The Coordinate Separation
* **Vertex Positions:** Passed in logical screen units (e.g., a window of 1280x720 logical points). A vertex shader projection matrix handles scaling logical points to Normalized Device Coordinates (NDC).
* **Hardware Scissor Rectangles:** The GPU hardware rasterizer register (`sg_apply_scissor_rect`) operates strictly in **physical framebuffer pixels** (e.g., 2560x1440 physical pixels on a 2.0x display).

### Automatic Coordinate Translation
To prevent clipping offsets on scaled displays, Chitra automatically maps scissor boundaries:
$$\text{physical\_x} = \text{logical\_x} \times \text{dpi\_scale}$$
$$\text{physical\_y} = \text{logical\_y} \times \text{dpi\_scale}$$
$$\text{physical\_w} = \text{logical\_w} \times \text{dpi\_scale}$$
$$\text{physical\_h} = \text{logical\_h} \times \text{dpi\_scale}$$

### Degenerate Scissor Protection
When a scrollable container scrolls completely out of view, its computed scissor width or height may become zero or negative. Hardware graphics drivers (Direct3D 11, Metal) can assert or produce undefined behavior on degenerate scissor rectangles. 
Chitra guards against this by clamping scissor dimensions to zero and bypassing draw submission for fully occluded elements.

---

## 8. Next Steps

To examine the concrete source code layout and understand how each file in the repository maps to these subsystems, continue to:
* **[Chapter 05: Source Tree and File Responsibilities](05_file_responsibilities.md)**: Repository structure, public headers, internal headers, backend implementations, and shader files.
