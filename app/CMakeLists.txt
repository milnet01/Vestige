## @file CMakeLists.txt
## @brief Build configuration for the Vestige application executable.

add_executable(vestige main.cpp)

target_link_libraries(vestige PRIVATE vestige_engine)

# Ensure assets are copied before running
add_dependencies(vestige copy_assets)

# -----------------------------------------------------------------------
# Editor launcher — stage the `vestige-editor` wrapper and `.desktop`
# file alongside the binary in the build tree so users can launch
# directly from the build output without `cmake --install`. Same files
# are picked up by install(DIRECTORY) when a system install is requested.
# -----------------------------------------------------------------------
add_custom_target(editor_launcher ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/packaging/vestige-editor.sh
        ${CMAKE_BINARY_DIR}/bin/vestige-editor
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/packaging/vestige-editor.desktop
        ${CMAKE_BINARY_DIR}/bin/vestige-editor.desktop
    COMMENT "Staging vestige-editor wrapper + .desktop into build/bin/"
)
add_dependencies(vestige editor_launcher)

# Install layout (only invoked via `cmake --install` / CPack):
#   ${prefix}/bin/vestige
#   ${prefix}/bin/vestige-editor
#   ${prefix}/share/applications/vestige-editor.desktop
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    install(TARGETS vestige RUNTIME DESTINATION bin)
    install(PROGRAMS ${CMAKE_SOURCE_DIR}/packaging/vestige-editor.sh
            DESTINATION bin
            RENAME vestige-editor)
    install(FILES ${CMAKE_SOURCE_DIR}/packaging/vestige-editor.desktop
            DESTINATION share/applications)
endif()
