include(FetchContent)

# ---------------------------------------------------------------------------
# Helper: silence informational deprecation warnings from upstream
# vendored dependencies that still declare an old cmake_minimum_required
# value. The warning is purely a hint to the upstream maintainer
# ("update your VERSION argument") and adds noise to our build log
# without indicating any real problem on our side.
#
# Usage:
#     _vestige_silence_upstream_dep_warnings()
#     FetchContent_MakeAvailable(some_old_dep)
#     _vestige_restore_dep_warnings()
#
# CMAKE_WARN_DEPRECATED is restored immediately after, so deprecation
# warnings about OUR code still surface normally.
# ---------------------------------------------------------------------------
macro(_vestige_silence_upstream_dep_warnings)
    set(_vestige_warn_dep_save ${CMAKE_WARN_DEPRECATED})
    set(CMAKE_WARN_DEPRECATED OFF CACHE BOOL "" FORCE)
endmacro()
macro(_vestige_restore_dep_warnings)
    set(CMAKE_WARN_DEPRECATED ${_vestige_warn_dep_save} CACHE BOOL "" FORCE)
    unset(_vestige_warn_dep_save)
endmacro()

# ---------------------------------------------------------------------------
# IF THIS BREAKS — FetchContent compatibility notes for future maintainers.
# ---------------------------------------------------------------------------
#
# Several FetchContent_Declare blocks below use the pattern:
#
#     FetchContent_Declare(name ... SOURCE_SUBDIR __engine_defines_target__)
#     FetchContent_MakeAvailable(name)
#
# This is a deliberate trick. The engine wants to populate (download +
# extract) a dependency without invoking add_subdirectory on its
# upstream CMakeLists — usually because the engine defines its own
# INTERFACE / STATIC library target from the populated source tree
# (e.g. imgui_lib, tinygltf, vestige_assets which has no CMakeLists at
# all). Pointing SOURCE_SUBDIR at a non-existent path tells
# FetchContent_MakeAvailable "populate but skip add_subdirectory".
#
# Why this pattern, and not FetchContent_Populate?
#   - CMake 3.27 deprecated direct FetchContent_Populate(name) calls
#     under policy CMP0169. The Populate function is scheduled for
#     eventual removal in some future CMake.
#   - SOURCE_SUBDIR has been stable since CMake 3.18; project min is
#     3.20. The trick is widely used for header-only / asset-only deps
#     and CMake has no announced deprecation for it.
#
# IF a future CMake breaks this pattern (e.g. by failing when
# SOURCE_SUBDIR points at a non-existent path), the migration paths in
# rough order of effort:
#
#   1. OVERRIDE_FIND_PACKAGE (CMake 3.24+) — declare the dep and route
#      a find_package() call through FetchContent. Only works for some
#      shapes (deps that already publish a *Config.cmake).
#
#   2. Use the dep's upstream CMakeLists.txt with EXCLUDE_FROM_ALL
#      (CMake 3.28+) — keeps the dep's targets out of the default ALL
#      target while letting us link them. Works when upstream has a
#      build system; the cost is that upstream's targets clutter the
#      project namespace.
#
#   3. ExternalProject_Add(... DOWNLOAD_ONLY ...) — completely
#      separate configure step from the parent build. Bulletproof but
#      heavy: requires a second cmake invocation.
#
#   4. A small CMake module wrapping file(DOWNLOAD) +
#      file(ARCHIVE_EXTRACT) — bypasses FetchContent entirely. Most
#      work to write but most resilient to FetchContent API churn.
#
#   5. Vendor the dep directly (commit it to external/<name>/) — same
#      trade-off as we already make for stb / glad / dr_libs. Loses
#      the version-pin benefit and adds repo size.
#
# Hedge: see the "CMake version matrix" note in ROADMAP.md
# Post-Release Commitments and docs/PRE_OPEN_SOURCE_AUDIT.md §8 — CI
# should test against multiple CMake versions to catch silent
# regressions before they hit production builds.
# ---------------------------------------------------------------------------

# GLFW — windowing and input
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
    GIT_SHALLOW    TRUE
)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

# GLM — mathematics
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
    GIT_SHALLOW    TRUE
)
_vestige_silence_upstream_dep_warnings()
FetchContent_MakeAvailable(glm)
_vestige_restore_dep_warnings()

# tinyexr — EXR image loading (single-header, like stb_image)
FetchContent_Declare(
    tinyexr
    GIT_REPOSITORY https://github.com/syoyo/tinyexr.git
    GIT_TAG        v1.0.9
    GIT_SHALLOW    TRUE
)
_vestige_silence_upstream_dep_warnings()
FetchContent_MakeAvailable(tinyexr)
_vestige_restore_dep_warnings()

# tinygltf — glTF 2.0 model loading
FetchContent_Declare(
    tinygltf
    GIT_REPOSITORY https://github.com/syoyo/tinygltf.git
    GIT_TAG        v2.9.4
    GIT_SHALLOW    TRUE
    # Skip add_subdirectory — the engine defines its own INTERFACE
    # target below using the populated source tree. SOURCE_SUBDIR
    # pointing at a non-existent path silences the CMP0169-deprecated
    # FetchContent_Populate path (CMake 3.18+; project min is 3.20).
    SOURCE_SUBDIR  __engine_defines_target__
)
FetchContent_MakeAvailable(tinygltf)
add_library(tinygltf INTERFACE)
target_include_directories(tinygltf INTERFACE ${tinygltf_SOURCE_DIR})
# Make source dir available to parent scope (for engine include paths)
set(tinygltf_SOURCE_DIR ${tinygltf_SOURCE_DIR} PARENT_SCOPE)

# FreeType — font rendering
FetchContent_Declare(
    freetype
    GIT_REPOSITORY https://github.com/freetype/freetype.git
    GIT_TAG        VER-2-13-3
    GIT_SHALLOW    TRUE
)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_ZLIB OFF CACHE BOOL "" FORCE)
_vestige_silence_upstream_dep_warnings()
FetchContent_MakeAvailable(freetype)
_vestige_restore_dep_warnings()

# Dear ImGui — immediate-mode GUI (docking branch for panel docking)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        docking
    GIT_SHALLOW    TRUE
    # Engine defines `imgui_lib` STATIC target below from the
    # populated tree — SOURCE_SUBDIR skip pattern (CMake 3.18+).
    SOURCE_SUBDIR  __engine_defines_target__
)
FetchContent_MakeAvailable(imgui)
add_library(imgui_lib STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui_lib PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui_lib PUBLIC glfw)
target_compile_options(imgui_lib PRIVATE -w)
set(imgui_SOURCE_DIR ${imgui_SOURCE_DIR} PARENT_SCOPE)

# ImGuizmo — 3D transform gizmos for the editor
FetchContent_Declare(
    imguizmo
    GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
    # Engine defines `imguizmo_lib` STATIC target below — SOURCE_SUBDIR skip.
    SOURCE_SUBDIR  __engine_defines_target__
)
FetchContent_MakeAvailable(imguizmo)
add_library(imguizmo_lib STATIC
    ${imguizmo_SOURCE_DIR}/ImGuizmo.cpp
)
target_include_directories(imguizmo_lib PUBLIC ${imguizmo_SOURCE_DIR})
target_link_libraries(imguizmo_lib PUBLIC imgui_lib)
target_compile_options(imguizmo_lib PRIVATE -w)
set(imguizmo_SOURCE_DIR ${imguizmo_SOURCE_DIR} PARENT_SCOPE)

# imgui-filebrowser — file browser dialog for ImGui (header-only, C++17 std::filesystem)
FetchContent_Declare(
    imgui_filebrowser
    GIT_REPOSITORY https://github.com/AirGuanZ/imgui-filebrowser.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
    # Engine defines `imgui_filebrowser` INTERFACE target below — SOURCE_SUBDIR skip.
    SOURCE_SUBDIR  __engine_defines_target__
)
FetchContent_MakeAvailable(imgui_filebrowser)
add_library(imgui_filebrowser INTERFACE)
target_include_directories(imgui_filebrowser INTERFACE ${imgui_filebrowser_SOURCE_DIR})
target_link_libraries(imgui_filebrowser INTERFACE imgui_lib)

# imgui-node-editor — node graph editor widget for visual scripting (MIT, Phase 9E-3)
# See docs/PHASE9E3_RESEARCH.md for the library survey + fallback strategy.
#
# Step 1 spike (2026-04-13):
# - Upstream v0.9.3 uses a few ImGui APIs that have since been removed or
#   deprecated in ImGui 1.92.x: ImRect::Floor() and ImGui::GetKeyIndex().
# - The fix is purely mechanical (5 + 5 call sites). Applied in-tree via
#   string-replace during configure so there's no separate .patch file to
#   maintain. The rewrite is idempotent (second runs are no-ops).
# - Tried pthom's imgui_bundle fork (fallback #1): it fixes Floor()/GetKeyIndex
#   but depends on custom ImGuiContextHookType_BeginWindow/EndWindow values
#   that exist only in pthom's ImGui fork, not upstream. Staying on thedmd
#   keeps us aligned with canonical ImGui.
FetchContent_Declare(
    imgui_node_editor
    GIT_REPOSITORY https://github.com/thedmd/imgui-node-editor.git
    GIT_TAG        v0.9.3
    GIT_SHALLOW    TRUE
    # Engine defines `imgui_node_editor_lib` STATIC target below
    # (after the ImGui 1.92.x compat string-rewrite) — SOURCE_SUBDIR skip.
    SOURCE_SUBDIR  __engine_defines_target__
)
FetchContent_MakeAvailable(imgui_node_editor)

# ---------------------------------------------------------------------------
# ImGui 1.92.x compatibility patch — see comment above.
# Idempotent: replacements on already-rewritten code are no-ops.
# ---------------------------------------------------------------------------
set(_ne_src "${imgui_node_editor_SOURCE_DIR}/imgui_node_editor.cpp")
if(EXISTS "${_ne_src}")
    file(READ "${_ne_src}" _ne_contents)

    # ImRect::Floor() was removed. Replace with explicit ImFloor on Min/Max.
    # Enumerate each distinct receiver — there are only seven across the file.
    foreach(_recv
        "node->m_GroupBounds"
        "node->m_Bounds"
        "newBounds"
        "m_NodeRect"
        "m_CurrentPin->m_Bounds"
        "m_GroupBounds")
        string(REPLACE "${_recv}.Floor();"
                       "${_recv}.Min = ImFloor(${_recv}.Min); ${_recv}.Max = ImFloor(${_recv}.Max);"
                       _ne_contents "${_ne_contents}")
    endforeach()
    unset(_recv)

    # ImGui::GetKeyIndex(ImGuiKey_Foo) is a deprecated-then-removed identity
    # function — ImGuiKey_Foo can be passed directly to IsKeyPressed().
    string(REGEX REPLACE "ImGui::GetKeyIndex\\((ImGuiKey_[A-Za-z0-9_]+)\\)"
                         "\\1"
                         _ne_contents "${_ne_contents}")

    file(WRITE "${_ne_src}" "${_ne_contents}")
    unset(_ne_contents)
endif()
unset(_ne_src)

# imgui_extra_math.inl defines ImVec2 ==, !=, and scalar*vec operators that
# now collide with definitions added to upstream ImGui (gated by
# IMGUI_DEFINE_MATH_OPERATORS, which node-editor sets). Guard the colliding
# definitions on the same flag ImGui uses to mark "operators already defined".
set(_ne_inl "${imgui_node_editor_SOURCE_DIR}/imgui_extra_math.inl")
if(EXISTS "${_ne_inl}")
    file(READ "${_ne_inl}" _ne_inl_contents)
    string(FIND "${_ne_inl_contents}" "VESTIGE_OP_GUARD" _ne_already_patched)
    if(_ne_already_patched EQUAL -1)
        string(REPLACE
            "inline bool operator==(const ImVec2& lhs, const ImVec2& rhs)\n{\n    return lhs.x == rhs.x && lhs.y == rhs.y;\n}"
            "// VESTIGE_OP_GUARD\n#ifndef IMGUI_DEFINE_MATH_OPERATORS_IMPLEMENTED\ninline bool operator==(const ImVec2& lhs, const ImVec2& rhs)\n{\n    return lhs.x == rhs.x && lhs.y == rhs.y;\n}\n#endif"
            _ne_inl_contents "${_ne_inl_contents}")
        string(REPLACE
            "inline bool operator!=(const ImVec2& lhs, const ImVec2& rhs)\n{\n    return lhs.x != rhs.x || lhs.y != rhs.y;\n}"
            "#ifndef IMGUI_DEFINE_MATH_OPERATORS_IMPLEMENTED\ninline bool operator!=(const ImVec2& lhs, const ImVec2& rhs)\n{\n    return lhs.x != rhs.x || lhs.y != rhs.y;\n}\n#endif"
            _ne_inl_contents "${_ne_inl_contents}")
        string(REPLACE
            "inline ImVec2 operator*(const float lhs, const ImVec2& rhs)\n{\n    return ImVec2(lhs * rhs.x, lhs * rhs.y);\n}"
            "#ifndef IMGUI_DEFINE_MATH_OPERATORS_IMPLEMENTED\ninline ImVec2 operator*(const float lhs, const ImVec2& rhs)\n{\n    return ImVec2(lhs * rhs.x, lhs * rhs.y);\n}\n#endif"
            _ne_inl_contents "${_ne_inl_contents}")
        file(WRITE "${_ne_inl}" "${_ne_inl_contents}")
    endif()
    unset(_ne_inl_contents)
    unset(_ne_already_patched)
endif()
unset(_ne_inl)

add_library(imgui_node_editor_lib STATIC
    ${imgui_node_editor_SOURCE_DIR}/imgui_node_editor.cpp
    ${imgui_node_editor_SOURCE_DIR}/imgui_node_editor_api.cpp
    ${imgui_node_editor_SOURCE_DIR}/imgui_canvas.cpp
    ${imgui_node_editor_SOURCE_DIR}/crude_json.cpp
)
target_include_directories(imgui_node_editor_lib PUBLIC ${imgui_node_editor_SOURCE_DIR})
target_link_libraries(imgui_node_editor_lib PUBLIC imgui_lib)
# Suppress warnings from third-party code (same policy as imgui_lib / imguizmo_lib).
target_compile_options(imgui_node_editor_lib PRIVATE -w)
set(imgui_node_editor_SOURCE_DIR ${imgui_node_editor_SOURCE_DIR} PARENT_SCOPE)

# nlohmann/json — JSON serialization (header-only, MIT)
# Use URL tarball to avoid cloning the ~150 MB repo
FetchContent_Declare(json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
)
FetchContent_MakeAvailable(json)

# Jolt Physics — physics engine (MIT license)
FetchContent_Declare(
    JoltPhysics
    GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
    GIT_TAG        v5.2.0
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  Build
)
set(TARGET_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(TARGET_HELLO_WORLD OFF CACHE BOOL "" FORCE)
set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "" FORCE)
set(TARGET_SAMPLES OFF CACHE BOOL "" FORCE)
set(TARGET_VIEWER OFF CACHE BOOL "" FORCE)
set(CPP_RTTI_ENABLED ON CACHE BOOL "" FORCE)
set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(JoltPhysics)

# ImPlot — 2D plotting extension for ImGui (MIT)
FetchContent_Declare(
    implot
    GIT_REPOSITORY https://github.com/epezent/implot.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(implot)
add_library(implot_lib STATIC
    ${implot_SOURCE_DIR}/implot.cpp
    ${implot_SOURCE_DIR}/implot_items.cpp
)
target_include_directories(implot_lib PUBLIC ${implot_SOURCE_DIR})
target_link_libraries(implot_lib PUBLIC imgui_lib)
target_compile_options(implot_lib PRIVATE -w)

# OpenAL Soft — spatial audio (LGPL 2.0+)
FetchContent_Declare(
    openal-soft
    GIT_REPOSITORY https://github.com/kcat/openal-soft.git
    GIT_TAG        1.24.1
    GIT_SHALLOW    TRUE
)
set(ALSOFT_UTILS OFF CACHE BOOL "" FORCE)
set(ALSOFT_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ALSOFT_INSTALL OFF CACHE BOOL "" FORCE)
set(ALSOFT_INSTALL_CONFIG OFF CACHE BOOL "" FORCE)
set(ALSOFT_INSTALL_HRTF_DATA OFF CACHE BOOL "" FORCE)
set(ALSOFT_INSTALL_AMBDEC_PRESETS OFF CACHE BOOL "" FORCE)
set(ALSOFT_INSTALL_UTILS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(openal-soft)

# Recast Navigation — navmesh generation and pathfinding (ZLib license)
FetchContent_Declare(
    recastnavigation
    GIT_REPOSITORY https://github.com/recastnavigation/recastnavigation.git
    GIT_TAG        v1.6.0
    GIT_SHALLOW    TRUE
)
set(RECASTNAVIGATION_DEMO OFF CACHE BOOL "" FORCE)
set(RECASTNAVIGATION_TESTS OFF CACHE BOOL "" FORCE)
set(RECASTNAVIGATION_EXAMPLES OFF CACHE BOOL "" FORCE)
# Recast's CMakeLists.txt uses cmake_minimum_required(VERSION 3.0) which is
# deprecated in CMake 3.27+. Allow it to configure anyway.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)
_vestige_silence_upstream_dep_warnings()
FetchContent_MakeAvailable(recastnavigation)
_vestige_restore_dep_warnings()
# Restore policy version for subsequent projects
unset(CMAKE_POLICY_VERSION_MINIMUM CACHE)

# Google Test — unit testing
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.15.2
    GIT_SHALLOW    TRUE
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# VestigeAssets — large CC0 textures (Poly Haven 4K diffuse JPGs,
# .blend.zip archives, extracted blend dirs). Split out from the
# engine repo to keep clones small. See ASSET_LICENSES.md and the
# top-level CMakeLists.txt copy_assets target — when enabled, it
# merges this repo's assets/ into the build output alongside the
# in-engine small assets.
#
# The repo has no CMakeLists.txt — Populate (not MakeAvailable) so
# CMake just clones it without trying to add_subdirectory. The
# clone path is exposed as ${vestige_assets_SOURCE_DIR}.
#
# Default OFF while milnet01/VestigeAssets is still private
# (pending full CC0 provenance re-audit before the v1.0.0 release).
# Fresh public clones build cleanly without authentication: the
# engine's demo scene uses the smaller in-engine 2K CC0 textures
# (Poly Haven plank_flooring_04, the glTF sample models, Arimo font)
# and falls back to flat-colour materials where the 4K variants
# would normally be. Set VESTIGE_FETCH_ASSETS=ON explicitly (and
# authenticate to the private repo) to opt in; once VestigeAssets
# goes public the default will flip back to ON.
option(VESTIGE_FETCH_ASSETS
    "Pull large CC0 assets from milnet01/VestigeAssets via FetchContent (private until ~v1.0.0)" OFF)
if(VESTIGE_FETCH_ASSETS)
    FetchContent_Declare(
        vestige_assets
        GIT_REPOSITORY https://github.com/milnet01/VestigeAssets.git
        GIT_TAG        v0.1.0
        GIT_SHALLOW    TRUE
        # SOURCE_SUBDIR points at a path that does not exist inside the
        # assets repo (which has no CMakeLists.txt). This tells the
        # newer FetchContent_MakeAvailable pattern to populate-only
        # without trying to add_subdirectory, and avoids the
        # CMP0169-deprecated direct call to FetchContent_Populate.
        # SOURCE_SUBDIR has been available since CMake 3.18; project
        # min is 3.20 so no bump needed.
        SOURCE_SUBDIR  __no_cmakelists_in_assets_repo__
    )
    FetchContent_MakeAvailable(vestige_assets)
    set(VESTIGE_ASSETS_DIR "${vestige_assets_SOURCE_DIR}" CACHE INTERNAL
        "Path to the populated VestigeAssets clone")
endif()
