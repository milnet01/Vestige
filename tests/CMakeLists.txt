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
)

target_link_libraries(vestige_tests
    PRIVATE
        vestige_engine
        GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(vestige_tests)
