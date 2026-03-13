## @file CMakeLists.txt
## @brief Build configuration for the Vestige engine static library.

# Collect all engine source files
set(ENGINE_SOURCES
    core/logger.cpp
    core/timer.cpp
    core/event_bus.cpp
    core/window.cpp
    core/input_manager.cpp
    core/engine.cpp
    core/first_person_controller.cpp
    scene/component.cpp
    scene/entity.cpp
    scene/mesh_renderer.cpp
    scene/scene.cpp
    scene/scene_manager.cpp
    resource/resource_manager.cpp
    renderer/shader.cpp
    renderer/mesh.cpp
    renderer/camera.cpp
    renderer/renderer.cpp
    renderer/texture.cpp
    renderer/material.cpp
    utils/obj_loader.cpp
    # glad OpenGL loader
    ${CMAKE_SOURCE_DIR}/external/glad/src/gl.c
    # stb_image implementation
    ${CMAKE_SOURCE_DIR}/external/stb/stb_image_impl.cpp
)

# Create static library
add_library(vestige_engine STATIC ${ENGINE_SOURCES})

# Include directories
target_include_directories(vestige_engine
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/external/glad/include
        ${CMAKE_SOURCE_DIR}/external/stb
)

# Link dependencies
target_link_libraries(vestige_engine
    PUBLIC
        glfw
        glm::glm
)

# Suppress warnings from third-party source files compiled as part of the engine
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/external/stb/stb_image_impl.cpp
    ${CMAKE_SOURCE_DIR}/external/glad/src/gl.c
    PROPERTIES COMPILE_FLAGS "-w"
)

# Compiler warnings
target_compile_options(vestige_engine PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:
        -Wall -Wextra -Wpedantic
        -Wformat=2
        -Wshadow
        -Wnull-dereference
        -Wdouble-promotion
    >
    $<$<AND:$<CXX_COMPILER_ID:GNU,Clang>,$<CONFIG:Release>>:
        -fstack-protector-strong
    >
)

# Debug sanitizers
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(vestige_engine PUBLIC
        $<$<CXX_COMPILER_ID:GNU,Clang>:
            -fsanitize=address
            -fsanitize=undefined
        >
    )
    target_link_options(vestige_engine PUBLIC
        $<$<CXX_COMPILER_ID:GNU,Clang>:
            -fsanitize=address
            -fsanitize=undefined
        >
    )
    # Suppress known third-party leaks (Mesa/GLFW driver allocations)
    set(ASAN_SUPPRESSIONS "${CMAKE_SOURCE_DIR}/asan_suppressions.txt")
    target_compile_definitions(vestige_engine PUBLIC
        VESTIGE_ASAN_SUPPRESSIONS="${ASAN_SUPPRESSIONS}"
    )
endif()
