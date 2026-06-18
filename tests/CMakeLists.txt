# Copyright (c) 2026 Anthony Schemel
# SPDX-License-Identifier: MIT

## @file CMakeLists.txt
## @brief Build configuration for Vestige unit tests.

add_executable(vestige_tests
    gl_test_fixture.cpp
    shader_parity_helpers.cpp
    test_shader_parity_smoke.cpp
    test_ibl_parity.cpp
    test_bloom_parity.cpp
    test_color_grading_parity.cpp
    test_volumetric_fog_gpu.cpp
    test_event_bus.cpp
    test_timer.cpp
    test_logger.cpp
    test_aabb.cpp
    test_entity.cpp
    test_entity_actions.cpp
    test_scene.cpp
    test_scene_deferred_mutation.cpp
    test_ui_focus_navigation.cpp
    test_framebuffer.cpp
    test_shadow_map.cpp
    test_tangent_calc.cpp
    test_point_shadow_map.cpp
    test_pom.cpp
    test_gltf_loader.cpp
    test_pbr_material.cpp
    test_bloom.cpp
    test_ssao.cpp
    test_transparency.cpp
    test_emissive_lighting.cpp
    test_taa.cpp
    test_text_rendering.cpp
    test_text_renderer_perf.cpp
    test_color_grading.cpp
    test_instanced_rendering.cpp
    test_cascaded_shadow_map.cpp
    test_ibl.cpp
    test_sh_probe_grid.cpp
    test_frustum_culling.cpp
    test_scene_serializer.cpp
    test_file_menu.cpp
    test_command_history.cpp
    test_panel_registry.cpp
    test_scene_envelope_atomicity.cpp
    test_particle_data.cpp
    test_animation_curve.cpp
    test_water_surface.cpp
    test_foliage_chunk.cpp
    test_spline_path.cpp
    test_biome_preset.cpp
    test_selection.cpp
    test_camera.cpp
    test_cpu_profiler.cpp
    test_memory_tracker.cpp
    test_obj_loader.cpp
    test_skeleton.cpp
    test_animation_sampler.cpp
    test_skeleton_animator.cpp
    test_cubicspline.cpp
    test_crossfade.cpp
    test_animation_state_machine.cpp
    test_root_motion.cpp
    test_easing.cpp
    test_tween.cpp
    test_ik_solver.cpp
    test_morph_target.cpp
    test_physics_world.cpp
    test_rigid_body.cpp
    test_physics_character_controller.cpp
    test_physics_constraint.cpp
    test_cloth_simulator.cpp
    test_cloth_presets.cpp
    test_camera_component.cpp
    test_morph_target_pipeline.cpp
    test_texture_filter.cpp
    test_ruler_tool.cpp
    test_density_map.cpp
    test_procedural_mesh.cpp
    test_facial_animation.cpp
    test_fabric_material.cpp
    test_viseme_map.cpp
    test_audio_analyzer.cpp
    test_lip_sync_player.cpp
    test_bvh.cpp
    test_spatial_hash.cpp
    test_cloth_mesh_collider.cpp
    test_cloth_simulation_collision.cpp
    test_cloth_solver_improvements.cpp
    test_gpu_particle_system.cpp
    test_motion_matching.cpp
    test_advanced_physics.cpp
    test_environment_forces.cpp
    test_formula_library.cpp
    test_formula_compiler.cpp
    test_curve_fitter.cpp
    test_quality_manager.cpp
    test_formula_tools.cpp
    test_architectural_tools.cpp
    test_catmull_rom_spline.cpp
    test_pressure_plate.cpp
    test_stasis_system.cpp
    test_system_registry.cpp
    test_domain_systems.cpp
    test_editor_viewers.cpp
    test_navigation_panel.cpp
    test_nav_mesh_query.cpp
    test_cloth_solver_backend.cpp
    test_cloth_constraint_graph.cpp
    test_cloth_wind_model.cpp
    test_cloth_backend_factory.cpp
    test_gpu_cloth_simulator.cpp
    test_cloth_cpu_gpu_parity.cpp
    test_ui_widgets.cpp
    test_ui_system_input.cpp
    test_ui_world_projection.cpp
    test_ui_design_widgets.cpp
    test_ui_theme_accessibility.cpp
    test_color_vision_filter.cpp
    test_fog.cpp
    test_volumetric_fog.cpp
    test_photosensitive_safety.cpp
    test_photosensitive_retrofit.cpp
    test_subtitle.cpp
    test_subtitle_renderer.cpp
    test_subtitle_narrator_style.cpp
    test_caption_map.cpp
    test_game_screen.cpp
    test_ui_system_screen_stack.cpp
    test_notification_queue.cpp
    test_default_hud.cpp
    test_ui_runtime_panel.cpp
    test_ui_accessible.cpp
    test_input_bindings.cpp
    test_settings.cpp
    test_first_run_wizard.cpp
    test_post_process_accessibility.cpp
    test_audio_attenuation.cpp
    test_audio_doppler.cpp
    test_audio_hrtf.cpp
    test_audio_occlusion.cpp
    test_audio_reverb.cpp
    test_audio_ambient.cpp
    test_audio_music.cpp
    test_audio_mixer.cpp
    test_audio_source_component.cpp
    test_audio_source_state.cpp
    test_audio_music_stream.cpp
    test_audio_music_player.cpp
    test_scene_load_audio_integration.cpp
    test_audio_panel.cpp
    test_camera_mode.cpp
    test_engine_paths.cpp
    test_component_clone.cpp
    test_entity_serializer_registry.cpp
    test_atomic_write_routing.cpp
    test_path_sandbox.cpp
    test_resource_manager_sandbox.cpp
    test_resource_manager_lru.cpp
    test_physics_layer_filter.cpp
    test_physics_fixed_step_callback.cpp
    test_audio_buffer_cache.cpp
    test_command_history_dirty.cpp
    test_audio_engine_sandbox.cpp
    test_lip_sync_sandbox.cpp
    test_gltf_bounds_checks.cpp
    test_gltf_fs_sandbox.cpp
    test_obj_mtl_warning.cpp
    test_cube_loader_hardening.cpp
    test_json_size_cap.cpp
    test_utf8.cpp
    test_rtl.cpp
    test_string_table.cpp
    test_localization_service.cpp
    test_result.cpp
    test_terrain_size_caps.cpp
    test_entity_serializer_depth_cap.cpp
    test_ibl_capture_sequence.cpp
    test_motion_overlay_prev_world.cpp
    test_normal_matrix.cpp
    test_audio_stop_sound.cpp
    test_scoped_shadow_depth_state.cpp
    test_bloom_downsample_karis.cpp
    test_sampler_fallback.cpp
    test_scoped_blend_state.cpp
    test_scoped_cull_face.cpp
    test_menu_prefabs.cpp
    test_ui_layout_panel.cpp
    test_ui_hit_test.cpp
    test_node_graph.cpp
    test_script_value.cpp
    test_blackboard.cpp
    test_node_type_registry.cpp
    test_script_graph.cpp
    test_script_context.cpp
    test_node_library.cpp
    test_scripting_system_bridge.cpp
    test_script_templates.cpp
    test_script_compiler.cpp
    test_sprite_atlas.cpp
    test_sprite_animation.cpp
    test_sprite_renderer.cpp
    test_physics2d_system.cpp
    test_tilemap.cpp
    test_camera_2d.cpp
    test_character_controller_2d.cpp
    test_game_templates_2d.cpp
    test_sprite_panel.cpp
    # FormulaWorkbench tool-side unit tests. The .cpp lives under
    # tools/formula_workbench/ and isn't part of vestige_engine, so
    # the test target compiles it directly.
    test_fit_history.cpp
    test_benchmark.cpp
    test_reference_harness.cpp
    test_async_driver.cpp
    test_pysr_parser.cpp
    test_markdown_render.cpp
    test_formula_node_editor_panel.cpp
    ${CMAKE_SOURCE_DIR}/tools/formula_workbench/fit_history.cpp
    ${CMAKE_SOURCE_DIR}/tools/formula_workbench/benchmark.cpp
    ${CMAKE_SOURCE_DIR}/tools/formula_workbench/reference_harness.cpp
    ${CMAKE_SOURCE_DIR}/tools/formula_workbench/async_driver.cpp
    ${CMAKE_SOURCE_DIR}/tools/formula_workbench/pysr_parser.cpp
    ${CMAKE_SOURCE_DIR}/tools/formula_workbench/markdown_render.cpp
    # Headless core of the FormulaNodeEditorPanel (sampler + state-only
    # methods). Split from formula_node_editor_panel.cpp so tests don't
    # need ImGui / ImPlot / imgui-node-editor linkage.
    ${CMAKE_SOURCE_DIR}/tools/formula_workbench/formula_node_editor_core.cpp
)

target_include_directories(vestige_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/tools/formula_workbench
)

# §3.4 reference-case harness needs to know where the JSON specs live.
# Pass the absolute source path so tests run from any cwd.
target_compile_definitions(vestige_tests
    PRIVATE
        VESTIGE_REFERENCE_CASES_DIR="${CMAKE_SOURCE_DIR}/tools/formula_workbench/reference_cases"
        # Phase 10.9 Sh2/Sh3 — cloth shader-source parity tests read the GLSL
        # files to pin CPU/GPU intent (no GL context in the test binary, so
        # the shaders themselves are the spec for the parity contract).
        VESTIGE_SHADER_DIR="${CMAKE_SOURCE_DIR}/assets/shaders"
        # Phase 10.9 W8 — streaming-music player tests read a committed
        # 0.5 s OGG fixture (decode/loop coverage runs headless, no device).
        VESTIGE_AUDIO_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures/audio"
        # Phase 10.9 W8 — committed pre-v2 scene fixture for the scene-format
        # v1→v2 backwards-compatible-load test.
        VESTIGE_SCENE_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures/scenes"
        # Phase 10 Localization L1 — bundled fonts dir for the Font ASCII
        # back-compat test (loads arimo.ttf under a real GL context).
        VESTIGE_FONT_DIR="${CMAKE_SOURCE_DIR}/assets/fonts"
        # Phase 10 Localization L4 — source-tree seed string-table dir so the
        # LocalizationService tests load {en,he,el,la}.json independent of cwd.
        VESTIGE_LOCALIZATION_DIR="${CMAKE_SOURCE_DIR}/assets/localization"
)

target_link_libraries(vestige_tests
    PRIVATE
        vestige_engine
        GTest::gtest_main
)

# Precompiled headers — GLM is the heaviest header shared across the test TUs;
# precompiling it cuts clean/multi-file build time. GoogleTest is deliberately
# NOT precompiled: it pulls in <string>, which trips a GCC 13 -Warray-bounds
# false positive in char_traits.h under -O3 + _FORTIFY_SOURCE=3 (Release) that
# -Werror turns fatal. GLM compiles clean at -O3.
target_precompile_headers(vestige_tests PRIVATE
    <glm/glm.hpp>
)

include(GoogleTest)
# No LSan suppression file: the only intentional third-party leaks (GLFW/Mesa
# init/teardown, llvmpipe JIT shader state) are excluded at their exact call
# sites via ScopedLeakCheckDisable (tests/lsan_guard.h), so the suite is
# leak-clean under default options with no environment setup.
gtest_discover_tests(vestige_tests)

# --- Phase 10 Localization L6 — string-coverage lint (design § 5.7) ----------
# The audit is a standalone Python regex line-scan, so it runs as its own ctest
# entries rather than inside the gtest binary.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_FOUND)
    # STRICT gate on the real tree — must pass (no hardcoded user-visible
    # strings, every literal tr() key present in en.json). Reviewer decision 4.
    add_test(
        NAME LocalizationAuditStrict
        COMMAND Python3::Interpreter
                ${CMAKE_SOURCE_DIR}/tools/localization_audit.py --strict
    )

    # Test 21 — the audit DETECTS a deliberately hardcoded literal: run it
    # against a fixture and expect a non-zero exit (WILL_FAIL inverts the pass
    # condition). Pins that strict mode actually fails on a violation.
    add_test(
        NAME LocalizationAuditCatchesHardcoded
        COMMAND Python3::Interpreter
                ${CMAKE_SOURCE_DIR}/tools/localization_audit.py --strict
                --root ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/localization_audit
                --reference ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/localization_audit/en.json
                --secondary ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/localization_audit/en.json
    )
    set_tests_properties(LocalizationAuditCatchesHardcoded
                         PROPERTIES WILL_FAIL TRUE)
else()
    message(WARNING
        "Python3 not found — skipping localization_audit ctest gate (L6).")
endif()
