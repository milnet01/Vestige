cmake_minimum_required(VERSION 3.21)
project(Vestige
    VERSION 0.1.60
    DESCRIPTION "Vestige 3D Engine"
    LANGUAGES CXX C
)

# C++17 standard, required
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Generate compile_commands.json for clang-tidy, IDEs, and tooling
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Build type defaults to Debug
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# Platform detection
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_compile_definitions(VESTIGE_PLATFORM_LINUX)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    add_compile_definitions(VESTIGE_PLATFORM_WINDOWS)
endif()

# Debug/Release definitions
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_definitions(VESTIGE_DEBUG)
elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_definitions(VESTIGE_RELEASE)
endif()

# ---------------------------------------------------------------------------
# Build-speed accelerators (ccache, fast linker, split DWARF)
#
# Mirrors the CI pipeline's ccache setup and adds local wins for the large
# Debug test binary, which is relinked on every test edit. Each piece degrades
# gracefully when its tool is missing; opt out with -DVESTIGE_FAST_BUILD=OFF.
# Must run before the add_subdirectory() calls so the compile/link options
# reach every target.
# ---------------------------------------------------------------------------
option(VESTIGE_FAST_BUILD "Use ccache / mold / split-DWARF when available" ON)
if(VESTIGE_FAST_BUILD)
    # Compiler cache — skip recompiling unchanged translation units. CI sets
    # the launcher explicitly; honour an existing one rather than overriding it.
    if(NOT CMAKE_CXX_COMPILER_LAUNCHER)
        find_program(CCACHE_PROGRAM ccache)
        if(CCACHE_PROGRAM)
            set(CMAKE_C_COMPILER_LAUNCHER   "${CCACHE_PROGRAM}" CACHE STRING "C compiler launcher" FORCE)
            set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}" CACHE STRING "C++ compiler launcher" FORCE)
            message(STATUS "Build speed: ccache enabled (${CCACHE_PROGRAM})")
        endif()
    endif()

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        # mold — far faster linking of the ~0.5 GB Debug test binary.
        find_program(MOLD_PROGRAM mold)
        if(MOLD_PROGRAM)
            add_link_options(-fuse-ld=mold)
            message(STATUS "Build speed: mold linker enabled (${MOLD_PROGRAM})")
        endif()

        # Split DWARF — keep debug info in .dwo side-files so relinks don't
        # recopy the huge .debug_* sections. Debug configs only.
        add_compile_options($<$<CONFIG:Debug>:-gsplit-dwarf>)
    endif()
endif()

# MSVC reads source files as the system code page by default, which corrupts the
# UTF-8 string + comment literals used throughout the codebase (e.g. em-dashes,
# π). /utf-8 makes the source + execution charset UTF-8 to match GCC/Clang.
# Correctness flag (not a fast-build opt-in), so applied unconditionally on MSVC.
if(MSVC)
    add_compile_options(/utf-8)
endif()

# ---------------------------------------------------------------------------
# Memory guard: cap concurrent heavy jobs (Ninja job pools)
#
# Each ASAN + PCH compile here peaks ~1.5 GB and the ~0.5 GB test-binary link
# is heavier still, so an unbounded `-j` (e.g. 12 on a dev box) can drive a
# parallel build into swap alongside an editor + browser. These pools cap
# concurrency regardless of the -j passed to the build, bounding peak RAM.
# Override with -DVESTIGE_COMPILE_JOBS=N / -DVESTIGE_LINK_JOBS=N when RAM is
# free. Ninja-only — other generators ignore JOB_POOLS.
# ---------------------------------------------------------------------------
set(VESTIGE_COMPILE_JOBS "6" CACHE STRING "Max concurrent compile jobs (Ninja job pool)")
set(VESTIGE_LINK_JOBS    "2" CACHE STRING "Max concurrent link jobs (Ninja job pool)")
if(CMAKE_GENERATOR MATCHES "Ninja")
    set_property(GLOBAL PROPERTY JOB_POOLS
        compile_jobs=${VESTIGE_COMPILE_JOBS}
        link_jobs=${VESTIGE_LINK_JOBS})
    set(CMAKE_JOB_POOL_COMPILE compile_jobs)
    set(CMAKE_JOB_POOL_LINK    link_jobs)
    message(STATUS "Build memory guard: compile pool=${VESTIGE_COMPILE_JOBS}, link pool=${VESTIGE_LINK_JOBS} (Ninja)")
endif()

# Option to build tests
option(VESTIGE_BUILD_TESTS "Build unit tests" ON)

# Fetch third-party dependencies
add_subdirectory(external)

# Engine library
add_subdirectory(engine)

# Application executable
add_subdirectory(app)

# Development tools (FormulaWorkbench, etc.)
option(VESTIGE_BUILD_TOOLS "Build development tools" ON)
if(VESTIGE_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

# Tests
if(VESTIGE_BUILD_TESTS)
    enable_testing()
    # When tests are enabled, compile the engine with VESTIGE_TEST_HOOKS so
    # the test binary can reach the AtomicWrite forced-failure seam (Phase
    # 10.9 Slice 12 Ed11 — scene-envelope atomicity rollback pin). The
    # hook is dormant unless armed from a test; production distribution
    # builds (with -DVESTIGE_BUILD_TESTS=OFF) omit the seam entirely.
    target_compile_definitions(vestige_engine PUBLIC VESTIGE_TEST_HOOKS)
    add_subdirectory(tests)
endif()

# Copy assets to build output.
#
# Two sources are merged into ${CMAKE_BINARY_DIR}/bin/assets:
#   1. ${CMAKE_SOURCE_DIR}/assets — small in-engine assets (shaders,
#      fonts, glTF sample models, 2K demo textures, block labels)
#   2. ${VESTIGE_ASSETS_DIR}/assets — large CC0 textures pulled from the
#      VestigeAssets repo via FetchContent (4K diffuse JPGs, .blend.zip
#      archives, extracted blend dirs). Optional; controlled by
#      VESTIGE_FETCH_ASSETS in external/CMakeLists.txt.
#
# Order matters: in-engine assets are copied SECOND so they take
# precedence on path collisions (none expected today, but keeps the
# engine repo authoritative for shared paths).
if(VESTIGE_FETCH_ASSETS AND VESTIGE_ASSETS_DIR AND EXISTS "${VESTIGE_ASSETS_DIR}/assets")
    add_custom_target(copy_assets ALL
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${VESTIGE_ASSETS_DIR}/assets
            ${CMAKE_BINARY_DIR}/bin/assets
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/assets
            ${CMAKE_BINARY_DIR}/bin/assets
        COMMENT "Copying assets (engine repo + VestigeAssets) to build directory"
    )
else()
    add_custom_target(copy_assets ALL
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/assets
            ${CMAKE_BINARY_DIR}/bin/assets
        COMMENT "Copying assets (engine repo only) to build directory"
    )
endif()
