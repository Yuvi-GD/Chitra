cmake_minimum_required(VERSION 3.20)
project(Chitra C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ==============================================================================
# SECTION 1: Backend Selection & Configuration
# ==============================================================================
# Chitra supports multiple rendering backends via Sokol.
# You can override this by passing -DCHITRA_BACKEND=<BACKEND> to CMake.
# ==============================================================================
set(CHITRA_BACKEND "AUTO" CACHE STRING "Sokol graphics backend (AUTO, D3D11, GLCORE, METAL, GLES3)")
set_property(CACHE CHITRA_BACKEND PROPERTY STRINGS AUTO D3D11 GLCORE METAL GLES3)

if(CHITRA_BACKEND STREQUAL "AUTO")
    if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        set(CHITRA_BACKEND_DEF "SOKOL_D3D11")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        set(CHITRA_BACKEND_DEF "SOKOL_METAL")
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
set(SHDC_EXECUTABLE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sokol-tools/bin/win32/sokol-shdc.exe")
set(SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/backends/shaders")
set(GEN_DIR "${SHADER_DIR}/gen")

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
        COMMAND ${SHDC_EXECUTABLE} -i ${INPUT_PATH} -o ${OUTPUT_PATH} -l glsl410:hlsl5:metal_macos
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
    src/chitra_arena.c
    src/chitra_cmd.c
    src/chitra_font.c
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
)

# Pass the backend selection macro to the C compiler (e.g., -DSOKOL_D3D11)
target_compile_definitions(chitra PUBLIC ${CHITRA_BACKEND_DEF})

# ==============================================================================
# SECTION 4: Sandbox Executable
# ==============================================================================
# A standalone executable for testing Chitra rendering independently.
# ==============================================================================
add_executable(chitra_sandbox sandbox/chitra_sandbox.c)
target_link_libraries(chitra_sandbox PRIVATE chitra)
target_include_directories(chitra_sandbox PRIVATE "${SOKOL_DIR}")

# Platform-specific system libraries required by Sokol
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(chitra_sandbox PRIVATE X11 Xi Xcursor GL pthread dl m)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_link_libraries(chitra_sandbox PRIVATE dwmapi)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    target_link_libraries(chitra_sandbox PRIVATE "-framework Cocoa" "-framework QuartzCore" "-framework Metal" "-framework MetalKit")
endif()
