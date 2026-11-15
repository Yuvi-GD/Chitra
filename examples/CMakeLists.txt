# ==============================================================================
# Chitra Standalone Examples & Tests
# ==============================================================================

# Standalone Chitra sandbox executable
add_executable(chitra_sandbox "${CMAKE_CURRENT_SOURCE_DIR}/../sandbox/chitra_sandbox.c")
target_link_libraries(chitra_sandbox PRIVATE chitra)
target_include_directories(chitra_sandbox PRIVATE "${SOKOL_DIR}")

# Platform-specific system libraries required by Sokol
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(chitra_sandbox PRIVATE X11 Xi Xcursor GL pthread dl m)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_link_libraries(chitra_sandbox PRIVATE dwmapi)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    target_link_libraries(chitra_sandbox PRIVATE "-framework Cocoa" "-framework QuartzCore" "-framework Metal" "-framework MetalKit")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    target_link_options(chitra_sandbox PRIVATE "-sUSE_WEBGL2=1")
    set_target_properties(chitra_sandbox PROPERTIES SUFFIX ".html")
endif()

# Headless Texture Pipeline Test
add_executable(test_texture "${CMAKE_CURRENT_SOURCE_DIR}/../tests/test_texture.c")
target_link_libraries(test_texture PRIVATE chitra)
target_include_directories(test_texture PRIVATE "${SOKOL_DIR}")
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_link_libraries(test_texture PRIVATE dwmapi)
endif()
