# Copyright (c) 2026 Anthony Schemel
# SPDX-License-Identifier: MIT

## @file CMakeLists.txt
## @brief Build configuration for the Vestige engine static library.

# Collect all engine source files
set(ENGINE_SOURCES
    core/logger.cpp
    core/timer.cpp
    core/event_bus.cpp
    core/system_registry.cpp
    core/job_system.cpp
    systems/atmosphere_system.cpp
    systems/particle_system.cpp
    systems/water_system.cpp
    systems/vegetation_system.cpp
    systems/terrain_system.cpp
    systems/cloth_system.cpp
    systems/destruction_system.cpp
    systems/character_system.cpp
    systems/footstep_system.cpp
    systems/impact_audio_system.cpp
    systems/lighting_system.cpp
    core/window.cpp
    core/input_manager.cpp
    core/engine.cpp
    core/engine_paths.cpp
    core/first_person_controller.cpp
    scene/component.cpp
    scene/entity.cpp
    scene/mesh_renderer.cpp
    scene/scene.cpp
    scene/scene_manager.cpp
    scene/camera_component.cpp
    scene/camera_mode.cpp
    scene/particle_emitter.cpp
    scene/gpu_particle_emitter.cpp
    scene/particle_presets.cpp
    scene/sprite_component.cpp
    scene/rigid_body_2d_component.cpp
    scene/collider_2d_component.cpp
    scene/tilemap_component.cpp
    scene/camera_2d_component.cpp
    scene/character_controller_2d_component.cpp
    scene/game_templates_2d.cpp
    resource/resource_manager.cpp
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
    renderer/font_stack.cpp
    renderer/text_renderer.cpp
    renderer/color_grading_lut.cpp
    renderer/color_grading_presets.cpp
    renderer/color_vision_filter.cpp
    renderer/fog.cpp
    renderer/volumetric_fog.cpp
    renderer/volumetric_fog_pass.cpp
    accessibility/photosensitive_safety.cpp
    accessibility/post_process_accessibility.cpp
    ui/subtitle.cpp
    ui/subtitle_renderer.cpp
    ui/caption_map.cpp
    ui/game_screen.cpp
    ui/ui_accessible.cpp
    input/input_bindings.cpp
    input/input_bindings_wire.cpp
    renderer/instance_buffer.cpp
    renderer/environment_map.cpp
    renderer/ibl_capture_sequence.cpp
    renderer/light_probe.cpp
    renderer/light_probe_manager.cpp
    renderer/sampler_fallback.cpp
    renderer/scoped_blend_state.cpp
    renderer/scoped_cull_face.cpp
    renderer/scoped_shadow_depth_state.cpp
    renderer/sh_probe_grid.cpp
    renderer/frame_diagnostics.cpp
    renderer/debug_draw.cpp
    renderer/depth_reducer.cpp
    renderer/smaa.cpp
    renderer/mesh_pool.cpp
    renderer/indirect_buffer.cpp
    renderer/particle_renderer.cpp
    renderer/gpu_particle_system.cpp
    renderer/sprite_atlas.cpp
    renderer/sprite_renderer.cpp
    renderer/tilemap_renderer.cpp
    resource/model.cpp
    platform/folder_dialog.cpp
    utils/asset_locator.cpp
    utils/obj_loader.cpp
    utils/gltf_loader.cpp
    utils/procedural_mesh.cpp
    utils/catmull_rom_spline.cpp
    utils/gltf_loader_impl.cpp
    utils/cube_loader.cpp
    utils/json_size_cap.cpp
    utils/utf8.cpp
    utils/rtl.cpp
    localization/string_table.cpp
    localization/localization_service.cpp
    utils/config_path.cpp
    utils/atomic_write.cpp
    utils/path_sandbox.cpp
    core/settings.cpp
    core/settings_migration.cpp
    core/settings_apply.cpp
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
    editor/panels/sprite_panel.cpp
    editor/panels/tilemap_panel.cpp
    editor/panels/localization_debug_panel.cpp
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
    editor/commands/group_entities_command.cpp
    editor/widgets/animation_curve.cpp
    editor/widgets/color_gradient.cpp
    editor/widgets/curve_editor_widget.cpp
    editor/widgets/gradient_editor_widget.cpp
    editor/widgets/node_editor_widget.cpp
    scene/water_surface.cpp
    renderer/water_renderer.cpp
    renderer/water_fbo.cpp
    renderer/foliage_renderer.cpp
    renderer/grass_renderer.cpp
    renderer/tree_renderer.cpp
    environment/environment_forces.cpp
    environment/density_map.cpp
    environment/foliage_chunk.cpp
    environment/foliage_manager.cpp
    environment/spline_path.cpp
    environment/biome_preset.cpp
    environment/terrain.cpp
    environment/terrain_material_set.cpp
    environment/meadow_terrain.cpp
    renderer/terrain_renderer.cpp
    formula/expression.cpp
    formula/formula.cpp
    formula/expression_eval.cpp
    formula/formula_library.cpp
    formula/physics_templates.cpp
    formula/audio_templates.cpp
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
    animation/sprite_animation.cpp
    # Phase 10.9 Slice 8 W12: animation zombie cluster relocated to
    # engine/experimental/animation/ — these compile into the same
    # vestige_engine library so existing tests link, but the new path
    # carries the "no production caller" semantic. To activate, plumb
    # one or more entry points (skeleton-driving demo, lip-sync-on-
    # audio-clip, motion-matched character controller) into the
    # game-side or editor surface and move the consumed files back to
    # animation/. See engine/experimental/animation/README.md.
    experimental/animation/facial_presets.cpp
    experimental/animation/eye_controller.cpp
    experimental/animation/facial_animation.cpp
    experimental/animation/viseme_map.cpp
    experimental/animation/audio_analyzer.cpp
    experimental/animation/lip_sync.cpp
    experimental/animation/feature_vector.cpp
    experimental/animation/kd_tree.cpp
    experimental/animation/motion_database.cpp
    experimental/animation/trajectory_predictor.cpp
    experimental/animation/inertialization.cpp
    experimental/animation/motion_matcher.cpp
    experimental/animation/motion_preprocessor.cpp
    experimental/animation/mirror_generator.cpp
    physics/physics_world.cpp
    physics/rigid_body.cpp
    physics/physics_character_controller.cpp
    physics/physics_constraint.cpp
    physics/physics_debug.cpp
    physics/cloth_simulator.cpp
    physics/cloth_wind_model.cpp
    physics/cloth_component.cpp
    physics/cloth_presets.cpp
    physics/fabric_material.cpp
    physics/bvh.cpp
    physics/cloth_mesh_collider.cpp
    physics/collider_generator.cpp
    physics/spatial_hash.cpp
    physics/deformable_mesh.cpp
    # Phase 10.9 Slice 8 W13: physics zombie cluster relocated to
    # engine/experimental/physics/ — same vestige_engine library, new
    # path carries the "no production caller" semantic. To activate,
    # plumb a real DestructionSystem::update + game-side / character
    # consumers (ragdoll-on-death, grab-and-throw, stasis-cast) and
    # move the consumed files back to physics/. See
    # engine/experimental/physics/README.md.
    experimental/physics/ragdoll_preset.cpp
    experimental/physics/ragdoll.cpp
    experimental/physics/grab_system.cpp
    experimental/physics/fracture.cpp
    experimental/physics/breakable_component.cpp
    experimental/physics/dismemberment_zones.cpp
    experimental/physics/dismemberment.cpp
    experimental/physics/stasis_system.cpp
    scene/interactable_component.cpp
    scene/pressure_plate_component.cpp
    renderer/dynamic_mesh.cpp
    profiler/gpu_timer.cpp
    profiler/cpu_profiler.cpp
    profiler/memory_tracker.cpp
    profiler/performance_profiler.cpp
    profiler/profile_log.cpp
    editor/panels/performance_panel.cpp
    editor/panels/validation_panel.cpp
    editor/panels/script_editor_panel.cpp
    editor/panels/welcome_panel.cpp
    editor/panels/texture_viewer_panel.cpp
    editor/panels/hdri_viewer_panel.cpp
    editor/panels/model_viewer_panel.cpp
    editor/panels/navigation_panel.cpp
    editor/panels/audio_panel.cpp
    editor/panels/fog_panel.cpp
    editor/panels/ui_layout_panel.cpp
    editor/panels/ui_runtime_panel.cpp
    editor/panels/template_dialog.cpp
    editor/panels/first_run_wizard.cpp
    editor/panels/settings_editor_panel.cpp
    editor/panels/panel_registry.cpp
    core/settings_editor.cpp
    editor/tools/ruler_tool.cpp
    editor/tools/wall_tool.cpp
    editor/tools/room_tool.cpp
    editor/tools/cutout_tool.cpp
    editor/tools/roof_tool.cpp
    editor/tools/stair_tool.cpp
    editor/tools/path_tool.cpp
    utils/entity_serializer.cpp
    utils/component_serializer_registry.cpp
    utils/material_library.cpp
    testing/visual_test_runner.cpp
    renderer/radiosity_baker.cpp
    # Audio system
    audio/audio_engine.cpp
    audio/audio_clip.cpp
    audio/audio_source_component.cpp
    audio/audio_attenuation.cpp
    audio/audio_doppler.cpp
    audio/audio_hrtf.cpp
    audio/audio_output_mode.cpp
    audio/audio_device_hotswap.cpp
    audio/audio_loudness.cpp
    audio/audio_air_absorption.cpp
    audio/audio_lod.cpp
    audio/audio_ducking.cpp
    audio/audio_occlusion.cpp
    audio/audio_reverb.cpp
    audio/reverb_ir_pool.cpp
    audio/acoustic_bake.cpp
    audio/acoustic_baker.cpp
    audio/audio_ambient.cpp
    audio/audio_music.cpp
    audio/audio_mixer.cpp
    audio/audio_spectrum.cpp
    audio/audio_mix_monitor.cpp
    audio/audio_music_stream.cpp
    audio/audio_music_player.cpp
    audio/audio_source_state.cpp
    audio/procedural/modal_synth.cpp
    audio/procedural/phisem.cpp
    audio/procedural/material_sound_bank.cpp
    systems/audio_system.cpp
    systems/audio_occlusion_system.cpp
    systems/reverb_system.cpp
    systems/music_system.cpp
    # UI/HUD system
    ui/sprite_batch_renderer.cpp
    ui/ui_element.cpp
    ui/ui_canvas.cpp
    ui/ui_label.cpp
    ui/ui_image.cpp
    ui/ui_panel.cpp
    ui/ui_crosshair.cpp
    ui/ui_progress_bar.cpp
    ui/ui_fps_counter.cpp
    ui/ui_world_projection.cpp
    ui/ui_world_label.cpp
    ui/ui_interaction_prompt.cpp
    ui/ui_theme.cpp
    ui/ui_button.cpp
    ui/ui_slider.cpp
    ui/ui_checkbox.cpp
    ui/ui_dropdown.cpp
    ui/ui_keybind_row.cpp
    ui/menu_prefabs.cpp
    ui/ui_notification_toast.cpp
    systems/ui_system.cpp
    # Phase 9F: 2D game support
    systems/sprite_system.cpp
    systems/physics2d_system.cpp
    # Navigation system
    navigation/nav_mesh_builder.cpp
    physics/cloth_backend_factory.cpp
    physics/cloth_constraint_graph.cpp
    physics/gpu_cloth_simulator.cpp
    navigation/nav_mesh_query.cpp
    navigation/nav_agent_component.cpp
    systems/navigation_system.cpp
    # Visual scripting system
    scripting/script_value.cpp
    scripting/pin_id.cpp
    scripting/blackboard.cpp
    scripting/node_type_registry.cpp
    scripting/script_graph.cpp
    scripting/script_instance.cpp
    scripting/script_context.cpp
    scripting/script_component.cpp
    scripting/scripting_system.cpp
    scripting/core_nodes.cpp
    scripting/event_nodes.cpp
    scripting/action_nodes.cpp
    scripting/pure_nodes.cpp
    scripting/flow_nodes.cpp
    scripting/latent_nodes.cpp
    scripting/script_templates.cpp
    scripting/script_compiler.cpp
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

# Precompiled headers — GLM and the hot STL containers appear in nearly every
# engine TU; precompiling them once speeds clean builds. Routed through a
# header file (not inline <angle> headers, which clash with the genexp's own
# <> delimiters) and scoped to C++ only — the target also compiles
# external/glad/src/gl.c, which must not receive a C++ PCH.
target_precompile_headers(vestige_engine PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/engine_pch.h>"
)

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

# tl::expected — vendored single-header fallback for engine/utils/result.h
# on the C++17 baseline (std::expected is C++23-only). SYSTEM so the strict
# -Werror flag set above does not police the third-party header.
target_include_directories(vestige_engine SYSTEM PUBLIC
    ${CMAKE_SOURCE_DIR}/external/tl_expected
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

# GLM vendored headers trigger -Wsign-conversion in qualifier.hpp (packed_highp
# array indexing on int literal). The CMake 3.25+ shorthand
# `set_target_properties(glm-header-only PROPERTIES SYSTEM TRUE)` is silently
# ignored on the 3.21.0 cmake-compat CI matrix runner — replicate the explicit
# Detour pattern instead: pull GLM's interface include dirs and re-add them
# to vestige_engine as SYSTEM PUBLIC so consumers see -isystem rather than -I.
foreach(_glm_target glm-header-only glm::glm glm)
    if(TARGET ${_glm_target})
        get_target_property(_glm_inc ${_glm_target} INTERFACE_INCLUDE_DIRECTORIES)
        if(_glm_inc)
            target_include_directories(vestige_engine SYSTEM PUBLIC ${_glm_inc})
            break()
        endif()
    endif()
endforeach()

# Link dependencies
target_link_libraries(vestige_engine
    PUBLIC
        glfw
        glm::glm
        tinyexr
        freetype
        imgui_lib
        implot_lib          # AX12 — audio spectrum/waveform viewer (Debug tab)
        imguizmo_lib
        imgui_filebrowser
        imgui_node_editor_lib
        nlohmann_json::nlohmann_json
        Jolt
        OpenAL::OpenAL
        ebur128
        enkiTS
        RecastNavigation::Recast
        RecastNavigation::Detour
        tinyfiledialogs
)
# enkiTS (MT2) carries its header dir as a SYSTEM INTERFACE include (set in
# external/CMakeLists.txt), so linking it above propagates <TaskScheduler.h>
# -Werror-safe — no extra include wiring needed here.

# libebur128 (AX9) is a C library; mark its header dir SYSTEM so the engine's
# -Werror/-Wconversion flag set doesn't police the third-party <ebur128.h>
# (same treatment as Jolt / Recast / GLM above).
get_target_property(EBUR128_INCLUDE_DIRS ebur128 INTERFACE_INCLUDE_DIRECTORIES)
if(EBUR128_INCLUDE_DIRS)
    target_include_directories(vestige_engine SYSTEM PUBLIC ${EBUR128_INCLUDE_DIRS})
endif()

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

# Compiler warnings (OpenSSF Compiler Hardening Guide compliance).
# -Werror is on: the 2026-04-19 L41 sweep drove the engine to zero
# warnings under this flag set, and -Werror locks that in — future
# regressions fail the build instead of silently accumulating. If a
# warning ever needs a justified suppression, prefer a #pragma at the
# narrowest possible scope; do not disable the flag globally.
target_compile_options(vestige_engine PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:
        -Werror
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

# Debug sanitizers. ENGINE_TSAN swaps ASan/UBSan for ThreadSanitizer — they are
# mutually exclusive — to race-check the MT2 job system (design D6) and future
# threaded subsystems (MT4). Default OFF (ASan/UBSan, the everyday build).
# Usage: cmake -B build-tsan -DCMAKE_BUILD_TYPE=Debug -DENGINE_TSAN=ON
option(ENGINE_TSAN "Build Debug with ThreadSanitizer instead of ASan/UBSan" OFF)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    if(ENGINE_TSAN)
        target_compile_options(vestige_engine PUBLIC
            $<$<CXX_COMPILER_ID:GNU,Clang>:-fsanitize=thread>
        )
        target_link_options(vestige_engine PUBLIC
            $<$<CXX_COMPILER_ID:GNU,Clang>:-fsanitize=thread>
        )
    else()
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
    endif()
    # No LSan suppression file: intentional third-party leaks (GLFW/Mesa driver
    # init/teardown, llvmpipe JIT shader state) are excluded at their exact call
    # sites via ScopedLeakCheckDisable (tests/lsan_guard.h).

    # Phase 10 Localization L4 — warn (once per session per key) on a missing
    # string-table lookup so translators/QA see coverage gaps. Debug-only;
    # Release omits it to keep StringTable::get branch-free (design § 5.5).
    # PUBLIC so the test binary compiles StringTable with the same member layout
    # (the macro gates a member — must match across TUs to avoid an ODR clash).
    target_compile_definitions(vestige_engine PUBLIC VESTIGE_LOCALIZATION_WARN_MISSING)
endif()
