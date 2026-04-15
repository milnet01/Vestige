include(FetchContent)

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
FetchContent_MakeAvailable(glm)

# tinyexr — EXR image loading (single-header, like stb_image)
FetchContent_Declare(
    tinyexr
    GIT_REPOSITORY https://github.com/syoyo/tinyexr.git
    GIT_TAG        v1.0.9
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(tinyexr)

# tinygltf — glTF 2.0 model loading
FetchContent_Declare(
    tinygltf
    GIT_REPOSITORY https://github.com/syoyo/tinygltf.git
    GIT_TAG        v2.9.4
    GIT_SHALLOW    TRUE
)
FetchContent_GetProperties(tinygltf)
if(NOT tinygltf_POPULATED)
    FetchContent_Populate(tinygltf)
endif()
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
FetchContent_MakeAvailable(freetype)

# Dear ImGui — immediate-mode GUI (docking branch for panel docking)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        docking
    GIT_SHALLOW    TRUE
)
FetchContent_GetProperties(imgui)
if(NOT imgui_POPULATED)
    FetchContent_Populate(imgui)
endif()
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
)
FetchContent_GetProperties(imguizmo)
if(NOT imguizmo_POPULATED)
    FetchContent_Populate(imguizmo)
endif()
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
)
FetchContent_GetProperties(imgui_filebrowser)
if(NOT imgui_filebrowser_POPULATED)
    FetchContent_Populate(imgui_filebrowser)
endif()
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
)
FetchContent_GetProperties(imgui_node_editor)
if(NOT imgui_node_editor_POPULATED)
    FetchContent_Populate(imgui_node_editor)
endif()

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
FetchContent_MakeAvailable(recastnavigation)
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
# top-level CMakeLists.txt copy_assets target — it merges this
# repo's assets/ into the build output alongside the in-engine
# small assets.
#
# The repo has no CMakeLists.txt — Populate (not MakeAvailable) so
# CMake just clones it without trying to add_subdirectory. The
# clone path is exposed as ${vestige_assets_SOURCE_DIR}.
#
# Set VESTIGE_FETCH_ASSETS=OFF to skip (e.g., offline builds, or
# when the assets repo is unreachable). The engine still builds
# and runs; the demo simply uses the smaller in-engine 2K
# variants and falls back to flat-colour materials where the 4K
# files would normally be.
option(VESTIGE_FETCH_ASSETS
    "Pull large CC0 assets from milnet01/VestigeAssets via FetchContent" ON)
if(VESTIGE_FETCH_ASSETS)
    FetchContent_Declare(
        vestige_assets
        GIT_REPOSITORY https://github.com/milnet01/VestigeAssets.git
        GIT_TAG        v0.1.0
        GIT_SHALLOW    TRUE
    )
    FetchContent_GetProperties(vestige_assets)
    if(NOT vestige_assets_POPULATED)
        FetchContent_Populate(vestige_assets)
    endif()
    set(VESTIGE_ASSETS_DIR "${vestige_assets_SOURCE_DIR}" CACHE INTERNAL
        "Path to the populated VestigeAssets clone")
endif()
