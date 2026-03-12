## @file CMakeLists.txt
## @brief Build configuration for the Vestige application executable.

add_executable(vestige main.cpp)

target_link_libraries(vestige PRIVATE vestige_engine)

# Ensure assets are copied before running
add_dependencies(vestige copy_assets)
