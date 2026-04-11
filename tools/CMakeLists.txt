## @file CMakeLists.txt
## @brief Build configuration for Vestige development tools.

# FormulaWorkbench — interactive formula discovery and fitting tool
add_executable(formula_workbench
    formula_workbench/main.cpp
    formula_workbench/workbench.cpp
)

target_include_directories(formula_workbench PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/formula_workbench
)

target_link_libraries(formula_workbench PRIVATE
    vestige_engine
    implot_lib
)

target_compile_options(formula_workbench PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:
        -Wall -Wextra -Wpedantic
        -fstack-protector-strong
        -D_GLIBCXX_ASSERTIONS
        -Wformat=2
        -Werror=format-security
    >
)
