# Chitra Engine: Documentation and Architecture Manual

> **Authoritative Technical Manual**  
> **Source Repository:** [https://github.com/Yuvi-GD/Chitra](https://github.com/Yuvi-GD/Chitra)  
> **Target Audience:** Core Engine Maintainers, Client Framework Developers, GUI Toolkit Authors, System Integrators, and AI Coding Assistants.

Welcome to the definitive architecture manual for **Chitra**, a high-performance, data-oriented 2D rendering engine implemented in pure C99.

This manual documents the entire architecture from high-level philosophy and memory models down to bitwise sort key layouts, vertex formats, and low-level Sokol GFX GPU batching.

---

## Manual Chapters

The manual is divided into 8 focused chapters designed to be read sequentially or consulted as an on-demand reference:

| Chapter | Title | Primary Focus |
| :---: | :--- | :--- |
| **01** | [Architecture Overview](manual/01_architecture_overview.md) | Mission, the 4-layer system stack, strict boundaries between Client Logic and Chitra, core design rules. |
| **02** | [Memory Model and Data Structures](manual/02_memory_model_and_structures.md) | The dynamic 2-table model (`CommandTable` + `free_list`), 16-byte slot, per-pipeline staging arrays, growth rates, compaction. |
| **03** | [Sorting and Draw Batching Pipeline](manual/03_sorting_and_batch_pipeline.md) | 64-bit integer Radix Sort key, 16-byte `Chitra_SortEntry`, 32-bit index buffer hole-skipping, 34-bit state coalescing bitmask (`0x00000003FFFFFFFF`). |
| **04** | [Asset Subsystems (Fonts and Vectors)](manual/04_asset_subsystems.md) | Modular font backends (FontStash vs. FreeType/HarfBuzz), single-upload atlas rule, decoupled text measurement, `earcut.h` SVG triangulation. |
| **05** | [Source Tree and File Responsibilities](manual/05_file_responsibilities.md) | Comprehensive map of every `.c` and `.h` file in the engine and their exact boundaries. |
| **06** | [API Reference and Client Integration](manual/06_api_reference_and_integration.md) | Signature-by-signature C API contract, client framework integration guide, lifecycle hooks, and working code examples. |
| **07** | [Architectural Decision Records (ADRs)](manual/07_architectural_decisions.md) | Detailed rationale and trade-offs behind every major architectural choice (the "Why"). |
| **08** | [Visual Lifecycles and Flowcharts](manual/08_visual_flowcharts.md) | Comprehensive Mermaid diagrams tracing element creation, in-place RAM updates, sort key collection, and GPU batch dispatch. |

---

## Core Rules and Invariants at a Glance

When writing or maintaining Chitra code, the following architectural invariants are non-negotiable:

1. **Strict Decoupling of Concerns:**
   - **Client Logic:** Calculates Flexbox/grid layout, manages widget hierarchies, handles input events, and holds plain 32-bit `handle`s. The client performs **zero graphics math, zero vertex generation, and zero rasterization**.
   - **Chitra (Renderer):** Owns staging RAM/VRAM, generates quads, manages texture atlases, clips via hardware scissor registers, sorts draw keys, and submits batched draw calls to the GPU.
2. **Dynamic Growth Multipliers:**
   - **Metadata Tables (`CommandTable` and `free_list`):** Grow by **1.5x** upon reaching capacity.
   - **Vertex Staging Arrays (`staging_arrays`):** Grow by **1.75x** upon compaction/reallocation to provide headroom for variable-size text append operations.
3. **Pluggable and Modular Subsystems:**
   - Chitra's architecture treats RHI (Sokol GFX vs. SDL_GPU), Platform HAL (Sokol App vs. SDL3), and Fonts (FontStash vs. FreeType/HarfBuzz) as interchangeable, isolated backends. The core frontend (`chitra_cmd.c`, `chitra_batch.c`) never changes when swapping backends.
4. **Hole-Skipping Index Buffer:**
   - Text updates and resizes leave dead memory holes in vertex arrays. Chitra **never** runs per-frame memory move operations on vertex memory. The 32-bit Index Buffer naturally skips dead holes because dead elements have no keys in the sort array.
5. **State-Aware Coalescing Bitmask:**
   - Batch flushing does not occur on Draw Order changes. Draw calls merge across different layers as long as `key & 0x00000003FFFFFFFFULL` (Clip, Pipeline, Texture) remains identical.

---

## Reading Guides by Role

### For Client Framework Developers
Start with:
1. [01. Architecture Overview](manual/01_architecture_overview.md) (System boundaries and mental model)
2. [06. API Reference and Client Integration](manual/06_api_reference_and_integration.md) (How to call Chitra from client applications)
3. [08. Visual Lifecycles and Flowcharts](manual/08_visual_flowcharts.md) (Visual trace of creation and frame updates)

### For Chitra Engine Maintainers
Start with:
1. [02. Memory Model and Data Structures](manual/02_memory_model_and_structures.md) (`CommandTable`, staging arrays, compaction)
2. [03. Sorting and Draw Batching Pipeline](manual/03_sorting_and_batch_pipeline.md) (Radix Sort, index buffer, state machine)
3. [05. Source Tree and File Responsibilities](manual/05_file_responsibilities.md) (File map and headers)
4. [07. Architectural Decision Records (ADRs)](manual/07_architectural_decisions.md) (Why things were built this way)

### For AI Coding Assistants
Before proposing code modifications or refactoring Chitra:
- Read [05. Source Tree and File Responsibilities](manual/05_file_responsibilities.md) to ensure changes target the correct file.
- Read [07. Architectural Decision Records (ADRs)](manual/07_architectural_decisions.md) to understand the technical rationales behind Chitra's data structures, sorting pipelines, and zero-allocation frame loop.

---