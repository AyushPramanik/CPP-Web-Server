add_executable(unit_tests
  test_logger.cpp
  test_buffer.cpp
  test_http_parser.cpp
  test_http_response.cpp
)

target_link_libraries(unit_tests
  PRIVATE
    cppws_core
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(unit_tests)
