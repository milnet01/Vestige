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
    scene/particle_emitter.cpp
    scene/particle_presets.cpp
    resource/resource_manager.cpp
    resource/async_texture_loader.cpp
    renderer/shader.cpp
    renderer/mesh.cpp
    renderer/camera.cpp
    renderer/renderer.cpp
    renderer/texture.cpp
    renderer/material.cpp
    renderer/framebuffer.cpp
    renderer/fullscreen_quad.cpp
    renderer/shadow_map.cpp
    renderer/cascaded_shadow_map.cpp
    renderer/skybox.cpp
    renderer/point_shadow_map.cpp
    renderer/taa.cpp
    renderer/font.cpp
    renderer/text_renderer.cpp
    renderer/color_grading_lut.cpp
    renderer/instance_buffer.cpp
    renderer/environment_map.cpp
    renderer/light_probe.cpp
    renderer/light_probe_manager.cpp
    renderer/sh_probe_grid.cpp
    renderer/frame_diagnostics.cpp
    renderer/debug_draw.cpp
    renderer/depth_reducer.cpp
    renderer/smaa.cpp
    renderer/mesh_pool.cpp
    renderer/indirect_buffer.cpp
    renderer/gpu_culler.cpp
    renderer/particle_renderer.cpp
    resource/model.cpp
    utils/obj_loader.cpp
    utils/gltf_loader.cpp
    utils/gltf_loader_impl.cpp
    utils/cube_loader.cpp
    editor/editor.cpp
    editor/editor_camera.cpp
    editor/entity_actions.cpp
    editor/entity_factory.cpp
    editor/selection.cpp
    editor/panels/hierarchy_panel.cpp
    editor/panels/history_panel.cpp
    editor/panels/import_dialog.cpp
    editor/panels/inspector_panel.cpp
    editor/panels/asset_browser_panel.cpp
    editor/prefab_system.cpp
    editor/command_history.cpp
    editor/file_menu.cpp
    editor/recent_files.cpp
    editor/scene_serializer.cpp
    editor/material_preview.cpp
    editor/tools/brush_tool.cpp
    editor/tools/brush_preview.cpp
    editor/tools/terrain_brush.cpp
    editor/panels/environment_panel.cpp
    editor/panels/terrain_panel.cpp
    editor/commands/terrain_sculpt_command.cpp
    editor/widgets/animation_curve.cpp
    editor/widgets/color_gradient.cpp
    editor/widgets/curve_editor_widget.cpp
    editor/widgets/gradient_editor_widget.cpp
    scene/water_surface.cpp
    renderer/water_renderer.cpp
    renderer/water_fbo.cpp
    renderer/foliage_renderer.cpp
    renderer/tree_renderer.cpp
    environment/foliage_chunk.cpp
    environment/foliage_manager.cpp
    environment/spline_path.cpp
    environment/biome_preset.cpp
    environment/terrain.cpp
    renderer/terrain_renderer.cpp
    animation/skeleton.cpp
    animation/animation_clip.cpp
    animation/animation_sampler.cpp
    animation/animation_state_machine.cpp
    animation/skeleton_animator.cpp
    animation/easing.cpp
    animation/tween.cpp
    animation/ik_solver.cpp
    animation/morph_target.cpp
    physics/physics_world.cpp
    physics/rigid_body.cpp
    physics/physics_debug.cpp
    profiler/gpu_timer.cpp
    profiler/cpu_profiler.cpp
    profiler/memory_tracker.cpp
    profiler/performance_profiler.cpp
    editor/panels/performance_panel.cpp
    utils/entity_serializer.cpp
    utils/material_library.cpp
    testing/visual_test_runner.cpp
    renderer/radiosity_baker.cpp
    # glad OpenGL loader
    ${CMAKE_SOURCE_DIR}/external/glad/src/gl.c
    # stb_image implementation
    ${CMAKE_SOURCE_DIR}/external/stb/stb_image_impl.cpp
    ${CMAKE_SOURCE_DIR}/external/stb/stb_image_write_impl.cpp
)

# Create static library
add_library(vestige_engine STATIC ${ENGINE_SOURCES})

# Include directories
target_include_directories(vestige_engine
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/external/glad/include
        ${CMAKE_SOURCE_DIR}/external/stb
        ${tinyexr_SOURCE_DIR}
        ${tinygltf_SOURCE_DIR}
    PRIVATE
        ${freetype_SOURCE_DIR}/include
)

# Mark Jolt headers as SYSTEM to suppress third-party warnings
get_target_property(JOLT_INCLUDE_DIRS Jolt INTERFACE_INCLUDE_DIRECTORIES)
if(JOLT_INCLUDE_DIRS)
    target_include_directories(vestige_engine SYSTEM PUBLIC ${JOLT_INCLUDE_DIRS})
endif()

# Link dependencies
target_link_libraries(vestige_engine
    PUBLIC
        glfw
        glm::glm
        tinyexr
        freetype
        imgui_lib
        imguizmo_lib
        imgui_filebrowser
        nlohmann_json::nlohmann_json
        Jolt
)

# Suppress warnings from third-party source files compiled as part of the engine
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/external/stb/stb_image_impl.cpp
    ${CMAKE_SOURCE_DIR}/external/stb/stb_image_write_impl.cpp
    ${CMAKE_SOURCE_DIR}/external/glad/src/gl.c
    ${CMAKE_CURRENT_SOURCE_DIR}/utils/gltf_loader_impl.cpp
    PROPERTIES COMPILE_FLAGS "-w"
)

# Compiler warnings
target_compile_options(vestige_engine PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:
        -Wall -Wextra -Wpedantic
        -Wformat=2
        -Wconversion
        -Wshadow
        -Wnull-dereference
        -Wdouble-promotion
        -fstack-protector-strong
    >
)

# Runtime buffer overflow detection (requires -O1 or higher)
target_compile_definitions(vestige_engine PRIVATE
    $<$<AND:$<CXX_COMPILER_ID:GNU,Clang>,$<NOT:$<CONFIG:Debug>>>:_FORTIFY_SOURCE=2>
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
