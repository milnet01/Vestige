# Copyright (c) 2026 Anthony Schemel
# SPDX-License-Identifier: MIT

## @file CMakeLists.txt
## @brief Build configuration for Vestige unit tests.

add_executable(vestige_tests
    test_event_bus.cpp
    test_timer.cpp
    test_logger.cpp
    test_aabb.cpp
    test_entity.cpp
    test_scene.cpp
    test_framebuffer.cpp
    test_shadow_map.cpp
    test_tangent_calc.cpp
    test_skybox.cpp
    test_point_shadow_map.cpp
    test_hdr_pipeline.cpp
    test_pom.cpp
    test_gltf_loader.cpp
    test_pbr_material.cpp
    test_bloom.cpp
    test_ssao.cpp
    test_transparency.cpp
    test_emissive_lighting.cpp
    test_async_texture_loader.cpp
    test_taa.cpp
    test_text_rendering.cpp
    test_color_grading.cpp
    test_instanced_rendering.cpp
    test_cascaded_shadow_map.cpp
    test_ibl.cpp
    test_sh_probe_grid.cpp
    test_frustum_culling.cpp
    test_scene_serializer.cpp
    test_file_menu.cpp
    test_command_history.cpp
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
    test_lip_sync.cpp
    test_cloth_collision.cpp
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
    test_cloth_solver_backend.cpp
    test_cloth_constraint_graph.cpp
    test_cloth_backend_factory.cpp
    test_gpu_cloth_simulator.cpp
    test_ui_widgets.cpp
    test_ui_system_input.cpp
    test_ui_world_projection.cpp
    test_ui_design_widgets.cpp
    test_ui_theme_accessibility.cpp
    test_color_vision_filter.cpp
    test_photosensitive_safety.cpp
    test_subtitle.cpp
    test_ui_accessible.cpp
    test_input_bindings.cpp
    test_post_process_accessibility.cpp
    test_menu_prefabs.cpp
    test_ui_layout_panel.cpp
    test_node_graph.cpp
    test_scripting.cpp
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
)

target_link_libraries(vestige_tests
    PRIVATE
        vestige_engine
        GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(vestige_tests
    PROPERTIES ENVIRONMENT "LSAN_OPTIONS=suppressions=${CMAKE_SOURCE_DIR}/asan_suppressions.txt"
)
