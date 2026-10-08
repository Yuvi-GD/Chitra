cmake_minimum_required(VERSION 3.20)
project(Chitra C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Define Sokol Include Path (from the new submodule)
set(SOKOL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sokol")

# 1. Define the Chitra Static Library
add_library(chitra STATIC
    src/chitra.c
    src/backends/sokol_hal.c
    src/backends/sokol_rhi.c
    src/backends/sokol_impl.c
)

# 2. Public Include Directory (for users of Chitra, e.g., Tether)
target_include_directories(chitra PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

# 3. Private Dependencies (Sokol)
target_include_directories(chitra PRIVATE
    "${SOKOL_DIR}"
    "${SOKOL_DIR}/util"
)

# 4. A Sandbox executable for testing Chitra independently of Tether
add_executable(chitra_sandbox
    sandbox/chitra_sandbox.c
)
target_link_libraries(chitra_sandbox PRIVATE chitra)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(chitra_sandbox PRIVATE X11 Xi Xcursor GL pthread dl m)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_link_libraries(chitra_sandbox PRIVATE dwmapi)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    target_link_libraries(chitra_sandbox PRIVATE "-framework Cocoa" "-framework QuartzCore" "-framework Metal" "-framework MetalKit")
endif()
