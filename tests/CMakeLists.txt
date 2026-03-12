## @file CMakeLists.txt
## @brief Build configuration for Vestige unit tests.

add_executable(vestige_tests
    test_event_bus.cpp
    test_timer.cpp
    test_logger.cpp
)

target_link_libraries(vestige_tests
    PRIVATE
        vestige_engine
        GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(vestige_tests)
