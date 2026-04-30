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
