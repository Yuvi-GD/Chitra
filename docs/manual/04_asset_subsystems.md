# Chapter 04: Asset Subsystems (Fonts and Vectors)

## 1. Overview of Asset Management in Chitra

A rendering engine must handle two primary categories of visual assets beyond simple geometric shapes:
1. **Typography:** Rasterizing font glyphs, managing texture atlases, shaping text runs, and computing layout bounds.
2. **Vector Graphics:** Ingesting arbitrary vector paths, polygons, and SVG curves without introducing heavy runtime dependencies.

Chitra decouples asset ingestion from vertex submission. Regardless of whether an asset originates from a TrueType font file or an SVG path, it is processed into standard 20-byte vertices and submitted through unified GPU pipelines.

```
+------------------------------------------------------------------------+
|                          ASSET INGESTION LAYER                         |
+-----------------------------------+------------------------------------+
| Typography Subsystem              | Vector Subsystem                   |
| - FontStash + stb_truetype (Lean) | - SVG Path Parsing                 |
| - FreeType + HarfBuzz (Enterprise)| - Polygon Contour Extraction       |
+-----------------------------------+------------------------------------+
                                    |
                                    v
+------------------------------------------------------------------------+
|                      PROCESSING & PACKING STAGE                        |
| - Dynamic Alpha Atlas Packing     | - Zero-Allocation Triangulation    |
| - Single-Upload GPU Sync          |   via earcut.h                     |
+-----------------------------------+------------------------------------+
                                    |
                                    v
+------------------------------------------------------------------------+
|                    UNIFIED CHITRA PIPELINE EMISSION                    |
| - Staging Array: Contiguous 20-byte Chitra_TextVertex quads & meshes   |
| - GPU Shaders: Texture/Mesh Pipeline (Pure modulation, zero branching) |
+------------------------------------------------------------------------+
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

## 4. Decoupled Text Measurement and Word Wrapping

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

## 5. The Vector Graphics Subsystem (SVG and Polygons)

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

## 6. High-DPI Displays and Scissor Coordinate Mapping

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

## 7. Next Steps

To examine the concrete source code layout and understand how each file in the repository maps to these subsystems, continue to:
* **[Chapter 05: Source Tree and File Responsibilities](05_file_responsibilities.md)**: Repository structure, public headers, internal headers, backend implementations, and shader files.
