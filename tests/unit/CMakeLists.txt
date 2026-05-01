add_executable(unit_tests
  test_logger.cpp
)

target_link_libraries(unit_tests
  PRIVATE
    cppws_core
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(unit_tests)
