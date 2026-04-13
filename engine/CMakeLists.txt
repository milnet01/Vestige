## @file CMakeLists.txt
## @brief Build configuration for the Vestige engine static library.

# Collect all engine source files
set(ENGINE_SOURCES
    core/logger.cpp
    core/timer.cpp
    core/event_bus.cpp
    core/system_registry.cpp
    systems/atmosphere_system.cpp
    systems/particle_system.cpp
    systems/water_system.cpp
    systems/vegetation_system.cpp
    systems/terrain_system.cpp
    systems/cloth_system.cpp
    systems/destruction_system.cpp
    systems/character_system.cpp
    systems/lighting_system.cpp
    core/window.cpp
    core/input_manager.cpp
    core/engine.cpp
    core/first_person_controller.cpp
    scene/component.cpp
    scene/entity.cpp
    scene/mesh_renderer.cpp
    scene/scene.cpp
    scene/scene_manager.cpp
    scene/camera_component.cpp
    scene/particle_emitter.cpp
    scene/gpu_particle_emitter.cpp
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
    renderer/gpu_particle_system.cpp
    resource/model.cpp
    utils/obj_loader.cpp
    utils/gltf_loader.cpp
    utils/procedural_mesh.cpp
    utils/catmull_rom_spline.cpp
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
    environment/environment_forces.cpp
    environment/density_map.cpp
    environment/foliage_chunk.cpp
    environment/foliage_manager.cpp
    environment/spline_path.cpp
    environment/biome_preset.cpp
    environment/terrain.cpp
    renderer/terrain_renderer.cpp
    formula/expression.cpp
    formula/formula.cpp
    formula/expression_eval.cpp
    formula/formula_library.cpp
    formula/physics_templates.cpp
    formula/codegen_cpp.cpp
    formula/codegen_glsl.cpp
    formula/lut_generator.cpp
    formula/lut_loader.cpp
    formula/curve_fitter.cpp
    formula/formula_preset.cpp
    formula/quality_manager.cpp
    formula/node_graph.cpp
    formula/sensitivity_analysis.cpp
    formula/formula_benchmark.cpp
    formula/formula_doc_generator.cpp
    animation/skeleton.cpp
    animation/animation_clip.cpp
    animation/animation_sampler.cpp
    animation/animation_state_machine.cpp
    animation/skeleton_animator.cpp
    animation/easing.cpp
    animation/tween.cpp
    animation/ik_solver.cpp
    animation/morph_target.cpp
    animation/facial_presets.cpp
    animation/eye_controller.cpp
    animation/facial_animation.cpp
    animation/viseme_map.cpp
    animation/audio_analyzer.cpp
    animation/lip_sync.cpp
    animation/feature_vector.cpp
    animation/kd_tree.cpp
    animation/motion_database.cpp
    animation/trajectory_predictor.cpp
    animation/inertialization.cpp
    animation/motion_matcher.cpp
    animation/motion_preprocessor.cpp
    animation/mirror_generator.cpp
    physics/physics_world.cpp
    physics/rigid_body.cpp
    physics/physics_character_controller.cpp
    physics/physics_constraint.cpp
    physics/physics_debug.cpp
    physics/cloth_simulator.cpp
    physics/cloth_component.cpp
    physics/cloth_presets.cpp
    physics/fabric_material.cpp
    physics/bvh.cpp
    physics/cloth_mesh_collider.cpp
    physics/collider_generator.cpp
    physics/spatial_hash.cpp
    physics/ragdoll_preset.cpp
    physics/ragdoll.cpp
    physics/grab_system.cpp
    physics/fracture.cpp
    physics/deformable_mesh.cpp
    physics/breakable_component.cpp
    physics/dismemberment_zones.cpp
    physics/dismemberment.cpp
    physics/stasis_system.cpp
    scene/interactable_component.cpp
    scene/pressure_plate_component.cpp
    renderer/dynamic_mesh.cpp
    profiler/gpu_timer.cpp
    profiler/cpu_profiler.cpp
    profiler/memory_tracker.cpp
    profiler/performance_profiler.cpp
    editor/panels/performance_panel.cpp
    editor/panels/validation_panel.cpp
    editor/panels/welcome_panel.cpp
    editor/panels/texture_viewer_panel.cpp
    editor/panels/hdri_viewer_panel.cpp
    editor/panels/model_viewer_panel.cpp
    editor/panels/template_dialog.cpp
    editor/tools/ruler_tool.cpp
    editor/tools/wall_tool.cpp
    editor/tools/room_tool.cpp
    editor/tools/cutout_tool.cpp
    editor/tools/roof_tool.cpp
    editor/tools/stair_tool.cpp
    editor/tools/path_tool.cpp
    resource/file_watcher.cpp
    utils/entity_serializer.cpp
    utils/material_library.cpp
    testing/visual_test_runner.cpp
    renderer/radiosity_baker.cpp
    # Audio system
    audio/audio_engine.cpp
    audio/audio_clip.cpp
    audio/audio_source_component.cpp
    systems/audio_system.cpp
    # UI/HUD system
    ui/sprite_batch_renderer.cpp
    ui/ui_element.cpp
    ui/ui_canvas.cpp
    ui/ui_label.cpp
    ui/ui_image.cpp
    ui/ui_panel.cpp
    systems/ui_system.cpp
    # Navigation system
    navigation/nav_mesh_builder.cpp
    navigation/nav_mesh_query.cpp
    navigation/nav_agent_component.cpp
    systems/navigation_system.cpp
    # Visual scripting system
    scripting/script_value.cpp
    scripting/blackboard.cpp
    scripting/node_type_registry.cpp
    scripting/script_graph.cpp
    scripting/script_instance.cpp
    scripting/script_context.cpp
    scripting/script_component.cpp
    scripting/scripting_system.cpp
    scripting/core_nodes.cpp
    # glad OpenGL loader
    ${CMAKE_SOURCE_DIR}/external/glad/src/gl.c
    # stb_image implementation
    ${CMAKE_SOURCE_DIR}/external/stb/stb_image_impl.cpp
    ${CMAKE_SOURCE_DIR}/external/stb/stb_image_write_impl.cpp
    # Audio decoder implementations (dr_libs + stb_vorbis)
    ${CMAKE_SOURCE_DIR}/external/dr_libs/dr_libs_impl.cpp
    ${CMAKE_SOURCE_DIR}/external/stb/stb_vorbis_impl.cpp
)

# Create static library
add_library(vestige_engine STATIC ${ENGINE_SOURCES})

# Include directories
target_include_directories(vestige_engine
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/external/glad/include
        ${CMAKE_SOURCE_DIR}/external/stb
        ${CMAKE_SOURCE_DIR}/external/dr_libs
        ${tinyexr_SOURCE_DIR}
        ${tinygltf_SOURCE_DIR}
    PRIVATE
        ${freetype_SOURCE_DIR}/include
)

# Mark third-party headers as SYSTEM to suppress their warnings
get_target_property(JOLT_INCLUDE_DIRS Jolt INTERFACE_INCLUDE_DIRECTORIES)
if(JOLT_INCLUDE_DIRS)
    target_include_directories(vestige_engine SYSTEM PUBLIC ${JOLT_INCLUDE_DIRS})
endif()

# Recast Navigation headers trigger -Wconversion in their inline functions
get_target_property(RECAST_INCLUDE_DIRS RecastNavigation::Recast INTERFACE_INCLUDE_DIRECTORIES)
if(RECAST_INCLUDE_DIRS)
    target_include_directories(vestige_engine SYSTEM PUBLIC ${RECAST_INCLUDE_DIRS})
endif()
get_target_property(DETOUR_INCLUDE_DIRS RecastNavigation::Detour INTERFACE_INCLUDE_DIRECTORIES)
if(DETOUR_INCLUDE_DIRS)
    target_include_directories(vestige_engine SYSTEM PUBLIC ${DETOUR_INCLUDE_DIRS})
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
        OpenAL::OpenAL
        RecastNavigation::Recast
        RecastNavigation::Detour
)

# Suppress warnings from third-party source files compiled as part of the engine
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/external/stb/stb_image_impl.cpp
    ${CMAKE_SOURCE_DIR}/external/stb/stb_image_write_impl.cpp
    ${CMAKE_SOURCE_DIR}/external/glad/src/gl.c
    ${CMAKE_CURRENT_SOURCE_DIR}/utils/gltf_loader_impl.cpp
    ${CMAKE_SOURCE_DIR}/external/dr_libs/dr_libs_impl.cpp
    ${CMAKE_SOURCE_DIR}/external/stb/stb_vorbis_impl.cpp
    PROPERTIES COMPILE_FLAGS "-w"
)

# Compiler warnings (OpenSSF Compiler Hardening Guide compliance)
target_compile_options(vestige_engine PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:
        -Wall -Wextra -Wpedantic
        -Wformat=2
        -Wconversion
        -Wsign-conversion
        -Wshadow
        -Wnull-dereference
        -Wdouble-promotion
        -Wimplicit-fallthrough
        -Werror=format-security
        -fstack-protector-strong
        -fstack-clash-protection
        -ftrivial-auto-var-init=zero
        -fno-delete-null-pointer-checks
        -fno-strict-overflow
    >
)

# Linker hardening (RELRO, non-executable stack, PIE)
target_link_options(vestige_engine PUBLIC
    $<$<CXX_COMPILER_ID:GNU,Clang>:
        -Wl,-z,relro
        -Wl,-z,now
        -Wl,-z,noexecstack
        -Wl,--as-needed
    >
)
target_compile_options(vestige_engine PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fPIE>
)

# Runtime buffer overflow detection and libstdc++ assertions
# _FORTIFY_SOURCE=3 requires GCC 12+ and -O1 or higher
# _GLIBCXX_ASSERTIONS enables precondition checks on container operations
target_compile_definitions(vestige_engine PRIVATE
    $<$<AND:$<CXX_COMPILER_ID:GNU,Clang>,$<NOT:$<CONFIG:Debug>>>:
        _FORTIFY_SOURCE=3
    >
    $<$<CXX_COMPILER_ID:GNU,Clang>:_GLIBCXX_ASSERTIONS>
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
