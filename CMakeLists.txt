cmake_minimum_required(VERSION 3.20)
project(Chitra C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
        set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
        set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    else()
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Debug/bin")
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release/bin")
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/RelWithDebInfo/bin")
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/MinSizeRel/bin")

        set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
        set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Debug/bin")
        set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release/bin")
        set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/RelWithDebInfo/bin")
        set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/MinSizeRel/bin")

        set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
        set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Debug/lib")
        set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release/lib")
        set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/RelWithDebInfo/lib")
        set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/MinSizeRel/lib")
    endif()
endif()

# ==============================================================================
# SECTION 1: Backend Selection & Configuration
# ==============================================================================
# Chitra supports multiple rendering backends via Sokol.
# You can override this by passing -DCHITRA_BACKEND=<BACKEND> to CMake.
# ==============================================================================
set(CHITRA_BACKEND "AUTO" CACHE STRING "Sokol graphics backend (AUTO, D3D11, GLCORE, METAL, GLES3, WGPU)")
set_property(CACHE CHITRA_BACKEND PROPERTY STRINGS AUTO D3D11 GLCORE METAL GLES3 WGPU)

if(CHITRA_BACKEND STREQUAL "AUTO")
    if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        set(CHITRA_BACKEND_DEF "SOKOL_D3D11")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin" OR CMAKE_SYSTEM_NAME STREQUAL "iOS")
        set(CHITRA_BACKEND_DEF "SOKOL_METAL")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Android" OR CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
        set(CHITRA_BACKEND_DEF "SOKOL_GLES3")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_SYSTEM_NAME MATCHES "BSD")
        set(CHITRA_BACKEND_DEF "SOKOL_GLCORE")
    else()
        set(CHITRA_BACKEND_DEF "SOKOL_GLCORE")
    endif()
else()
    set(CHITRA_BACKEND_DEF "SOKOL_${CHITRA_BACKEND}")
endif()

message(STATUS "[Chitra] Configured to use backend: ${CHITRA_BACKEND_DEF}")

# ==============================================================================
# SECTION 2: Shader Generation (sokol-shdc)
# ==============================================================================
# We compile GLSL shaders into C headers for multiple target backends.
# ==============================================================================
set(SOKOL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sokol")
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    set(SHDC_EXECUTABLE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sokol-tools/bin/win32/sokol-shdc.exe")
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "arm|aarch64")
        set(SHDC_EXECUTABLE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sokol-tools/bin/osx_arm64/sokol-shdc")
    else()
        set(SHDC_EXECUTABLE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sokol-tools/bin/osx/sokol-shdc")
    endif()
else()
    if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "arm|aarch64")
        set(SHDC_EXECUTABLE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sokol-tools/bin/linux_arm64/sokol-shdc")
    else()
        set(SHDC_EXECUTABLE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sokol-tools/bin/linux/sokol-shdc")
    endif()
endif()
set(SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/backends/shaders")
set(GEN_DIR "${SHADER_DIR}/gen")
file(MAKE_DIRECTORY ${GEN_DIR})

set(SHADER_FILES
    "chitra_shape.glsl"
    "chitra_text.glsl"
)

set(GENERATED_HEADERS "")

foreach(SHADER_FILE ${SHADER_FILES})
    set(INPUT_PATH "${SHADER_DIR}/${SHADER_FILE}")
    set(OUTPUT_PATH "${GEN_DIR}/${SHADER_FILE}.h")
    
    add_custom_command(
        OUTPUT ${OUTPUT_PATH}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${GEN_DIR}
        COMMAND ${SHDC_EXECUTABLE} -i ${INPUT_PATH} -o ${OUTPUT_PATH} -l glsl410:hlsl5:metal_macos:metal_ios:metal_sim:glsl300es:wgsl
        DEPENDS ${INPUT_PATH}
        COMMENT "Compiling Shader: ${SHADER_FILE} -> ${OUTPUT_PATH}"
    )
    list(APPEND GENERATED_HEADERS ${OUTPUT_PATH})
endforeach()

add_custom_target(chitra_shaders DEPENDS ${GENERATED_HEADERS})

# ==============================================================================
# SECTION 3: Chitra Static Library
# ==============================================================================
# Compiles the core engine and links it against the Sokol backend.
# ==============================================================================
add_library(chitra STATIC
    src/chitra.c
    src/chitra_table.c
    src/chitra_shape.c
    src/chitra_texture.c
    src/chitra_font.c
    src/chitra_batch.c
    src/chitra_clip.c
    src/backends/sokol_hal.c
    src/backends/sokol_rhi.c
    src/backends/sokol_impl.c
)
add_dependencies(chitra chitra_shaders)

# Public includes (exposed to users of Chitra)
target_include_directories(chitra PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

# Private includes (internal dependencies like Sokol)
target_include_directories(chitra PRIVATE
    "${SOKOL_DIR}"
    "${SOKOL_DIR}/util"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/fontstash"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb"
)

# Pass the backend selection macro to the C compiler (e.g., -DSOKOL_D3D11)
target_compile_definitions(chitra PUBLIC ${CHITRA_BACKEND_DEF})

if(WIN32)
    # Windows Desktop (Direct3D 11 default)
    target_link_libraries(chitra PUBLIC dwmapi)
    if(CHITRA_BACKEND_DEF STREQUAL "SOKOL_GLCORE")
        target_link_libraries(chitra PUBLIC opengl32 gdi32)
    endif()
elseif(APPLE)
    set_source_files_properties(src/backends/sokol_impl.c PROPERTIES COMPILE_FLAGS "-x objective-c")
    if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        # Apple iOS (Metal)
        target_link_libraries(chitra PUBLIC
            "-framework UIKit"
            "-framework Metal"
            "-framework MetalKit"
            "-framework QuartzCore"
            "-framework AudioToolbox"
            "-framework AVFoundation"
        )
    else()
        # Apple macOS (Metal)
        target_link_libraries(chitra PUBLIC
            "-framework Cocoa"
            "-framework Metal"
            "-framework MetalKit"
            "-framework QuartzCore"
            "-framework AudioToolbox"
        )
    endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Android")
    # Android (OpenGLES 3)
    target_link_libraries(chitra PUBLIC
        GLESv3
        EGL
        android
        log
        m
    )
elseif(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    # WebAssembly (WebGL2 or WebGPU)
    if(CHITRA_BACKEND_DEF STREQUAL "SOKOL_GLES3")
        target_link_options(chitra PUBLIC "-sUSE_WEBGL2=1")
    elseif(CHITRA_BACKEND_DEF STREQUAL "SOKOL_WGPU")
        target_compile_options(chitra PUBLIC "--use-port=emdawnwebgpu")
        target_link_options(chitra PUBLIC "--use-port=emdawnwebgpu" "-sDEFAULT_TO_CXX")
    endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_SYSTEM_NAME MATCHES "BSD")
    # Linux Desktop (X11 / OpenGL Core or OpenGLES 3)
    if(CHITRA_BACKEND_DEF STREQUAL "SOKOL_GLES3")
        target_link_libraries(chitra PUBLIC X11 Xi Xcursor GLESv2 EGL pthread dl m)
    else()
        target_link_libraries(chitra PUBLIC X11 Xi Xcursor GL pthread dl m)
    endif()
else()
    # Generic POSIX fallback
    target_link_libraries(chitra PUBLIC pthread dl m)
endif()

# ==============================================================================
# SECTION 4: Examples & Standalone Tools
# ==============================================================================
option(CHITRA_BUILD_EXAMPLES "Build Chitra standalone sandbox and examples" OFF)
if(CHITRA_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()


