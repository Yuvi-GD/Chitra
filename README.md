# Chitra 🎨

[![Language](https://img.shields.io/badge/language-C99-blue.svg)](https://en.wikipedia.org/wiki/C99)
[![License](https://img.shields.io/badge/license-Apache_2.0-blue.svg)](LICENSE)
[![Build](https://img.shields.io/badge/build-CMake-green.svg)](CMakeLists.txt)
[![Architecture](https://img.shields.io/badge/design-Data--Oriented-orange.svg)](docs/index.md)
[![Docs](https://img.shields.io/badge/docs-manual-orange.svg)](docs/index.md)

**Chitra** is a lean, ultra-high-performance 2D rendering engine implemented in pure C99. Designed from the ground up on modern Data-Oriented Design (DOD) principles, Chitra delivers uncompromising high-FPS rendering with minimal CPU overhead, zero scene-tree duplication, and hardware-accelerated draw batching.

Chitra can function as the rendering engine for retained UI frameworks (such as [Tether](https://github.com/Yuvi-GD/Tether)) or as a standalone immediate-mode vector and UI graphics engine.

---

## Features

* **O(0) Static Cost:** Unchanged UI elements cost zero CPU work per frame. No recursive tree traversals and no re-rasterization.
* **O(1) In-Place Updates:** Position, size, border radius, and color updates overwrite contiguous memory directly via stable 32-bit element IDs.
* **Dynamic 2-Table Memory Architecture:** An ultra-lean 16-byte `CommandTable` (Slot Map) paired with a LIFO `free_list` recycler. Zero hardcoded limits with dynamic heap growth.
* **Per-Pipeline Staging Arrays:** Contiguous, cache-friendly vertex buffers per shader pipeline (40-byte Shape SDF, 20-byte Text/Texture, 20-byte Mesh).
* **32-Bit Index Buffer Hole-Skipping:** Dynamic text updates and element removals leave dead memory holes in RAM/VRAM that are naturally bypassed by the GPU index stream, completely eliminating expensive per-frame memory shifting (`memmove`).
* **Microsecond 64-Bit Radix Sorting:** Coalesces draw calls and minimizes GPU state transitions using a compact 64-bit sort key `[ Z-Index | Scissor | Pipeline | Texture | Command ID ]`.
* **Hardware Fixed-Function Scissoring:** Direct GPU hardware scissor registers instead of expensive fragment shader discards.
* **Pluggable and Modular Architecture:**
  * **Graphics RHI:** Sokol GFX (Direct3D 11, Metal, WebGPU, OpenGL) today, with future support for SDL_GPU (Direct3D 12, Metal, Vulkan).
  * **Platform HAL:** Sokol App today, with future support for SDL3.
  * **Font Subsystem:** Embedded FontStash (`stb_truetype`) today, with modular support for FreeType and HarfBuzz.
  * **Vector Subsystem:** Zero-allocation triangulation via `earcut.h` for complex SVG paths without monolithic C++ software rasterizers.

---

## Architecture Overview

```
+------------------------------------------------------------------------+
|                   Layer 3: Client Application                          |
|  - UI Framework (e.g., Tether), Game Engine, or Standalone Tool        |
|  - Manages UI logic, layouts, events, and holds 32-bit Element IDs     |
+-----------------------------------+-+----------------------------------+
                                    |
+-----------------------------------v------------------------------------+
|                   Layer 2: Chitra Engine Core                          |
|  - Dynamic 2-Table Model: CommandTable (Slot Map) and free_list        |
|  - Per-Pipeline Vertex Staging Arrays (RAM)                            |
|  - Modular Font and Vector Asset Subsystems                            |
|  - 64-Bit Radix Sort Key Generation and Microsecond Integer Sort       |
|  - 32-Bit Index Buffer Hole-Skipping and Draw Call Batch Coalescing    |
+-----------------------------------+-+----------------------------------+
                                    |
+-----------------------------------v------------------------------------+
|               Layer 1: Render Hardware Interface (RHI)                 |
|  - CURRENT: Sokol GFX (Direct3D 11, Metal, WebGPU, OpenGL ES)          |
|  - FUTURE:  SDL_GPU (Direct3D 12, Metal, Vulkan)                       |
+-----------------------------------+-+----------------------------------+
                                    |
+-----------------------------------v------------------------------------+
|                    Layer 0: Platform HAL (Window & Input)              |
|  - CURRENT: Sokol App (Ultra-lean single window, cross-platform)       |
|  - FUTURE:  SDL3 (Multi-window, gamepads, native event waiting)        |
+------------------------------------------------------------------------+
```

---

## Prerequisites

To build Chitra from source, ensure you have:

* **Compiler:** A C99-compliant compiler (MSVC 2019+, GCC 9+, Clang 10+, or AppleClang).
* **Build System:** CMake 3.20 or newer.
* **Platform Dependencies:**
  * **Windows:** Windows SDK with Direct3D 11 runtime (preinstalled on Windows 10/11).
  * **Linux:** Standard X11/Wayland development libraries and OpenGL/Mesa headers:
    ```bash
    sudo apt update
    sudo apt install -y build-essential cmake libx11-dev libxi-dev libxcursor-dev libgl1-mesa-dev
    ```
  * **macOS:** Xcode Command Line Tools (Metal framework is included with macOS).

---

## Building from Source

Chitra is an independent, standalone project:

```bash
# Clone the repository
git clone https://github.com/Yuvi-GD/Chitra.git
cd Chitra

# Generate build configuration
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build the library and sandbox
cmake --build build --config Release
```

The compiled binaries will be located in `build/` (or `build/Release/` on Windows with MSVC).

---

## Quick Start Example

Here is a minimal standalone example showing how to initialize Chitra, create retained UI elements, perform in-place updates, and render:

```c
#include "chitra.h"

int main(void) {
    // 1. Initialize Chitra with viewport size
    chitra_init(1280, 720);

    // 2. Register a scissor clip region (optional, 0 = full window)
    uint8_t clip_id = chitra_register_clip(0, 0, 1280, 720);

    // 3. Create retained elements
    uint32_t panel_id = chitra_create_rect(
        100.0f, 100.0f, 400.0f, 300.0f, // X, Y, Width, Height
        12.0f, 2.0f,                    // Corner Radius, Border Width
        0xFF2A2D34, 0xFF4F5D75,         // Fill Color, Border Color (RGBA)
        10, clip_id                     // Z-Index, Clip ID
    );

    uint32_t text_id = chitra_create_text(
        "Hello Chitra!",
        120.0f, 130.0f, 24.0f, 360.0f,  // X, Y, Font Size, Wrap Width
        0xFFFFFFFF,                     // Text Color
        20, clip_id                     // Z-Index, Clip ID
    );

    // 4. Update elements in place when dirty (O(1) memory write)
    chitra_update_rect(panel_id, 100.0f, 100.0f, 450.0f, 300.0f, 12.0f, 2.0f, 0xFF2A2D34, 0xFF4F5D75);

    // 5. Frame render callback
    chitra_begin_frame(1280, 720, 1.0f);
    chitra_render(); // Radix sort, index generation, batch coalescing, GPU draw
    chitra_end_frame();

    // 6. Cleanup on exit
    chitra_shutdown();
    return 0;
}
```

---

## Documentation

Comprehensive architecture specifications, memory models, and integration guides are documented in the [`docs/`](docs/) directory:

* **[Documentation Index](docs/index.md):** The master table of contents and reading guides for engineers and AI systems.
* Detailed chapters covering memory layout, Radix sorting, asset pipelines, file responsibilities, C API contracts, and visual flowcharts are all organized within [`docs/manual/`](docs/manual/).

---

## Third-Party Libraries and Acknowledgements

Chitra relies on several outstanding lightweight C libraries:

* **[Sokol](https://github.com/floooh/sokol)** by Andre Weissflog (@floooh) - Minimal, cross-platform graphics (`sokol_gfx`) and application windowing (`sokol_app`) headers.
* **[FontStash](https://github.com/memononen/fontstash)** by Mikko Mononen (@memononen) - Dynamic font texture atlas management and quad emission.
* **[stb_truetype](https://github.com/nothings/stb)** by Sean Barrett (@nothings) - Embedded TrueType font parsing and rasterization.

---

## Contributing

Contributions, bug reports, and discussions are welcome.

* If you encounter an issue or have a feature proposal, please open an issue on GitHub.
* Pull requests should adhere to pure C99 syntax, follow the project's Data-Oriented Design principles, and avoid adding heavy external dependencies.

---

## License

Chitra is open-source software licensed under the **Apache License, Version 2.0**. See the [LICENSE](LICENSE) file for the full license text.
