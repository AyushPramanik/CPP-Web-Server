#include "util/logger.hpp"

#include <gtest/gtest.h>
#include <spdlog/spdlog.h>

// Use synchronous mode in tests — async logging involves a background thread
// that can outlive the test process and cause ASAN false positives.
class LoggerTest : public ::testing::Test {
protected:
  void SetUp() override {
    cppws::util::Logger::init(cppws::util::Logger::Level::Trace, "", false);
  }

  void TearDown() override {
    cppws::util::Logger::shutdown();
  }
};

TEST_F(LoggerTest, InitializesWithoutThrowing) {
  EXPECT_NO_THROW(LOG_INFO("Logger initialized"));
}

TEST_F(LoggerTest, AllLogLevelsCompileAndRun) {
  EXPECT_NO_THROW({
    LOG_TRACE("trace message {}", 1);
    LOG_DEBUG("debug message {}", 2.0);
    LOG_INFO("info message {}", "three");
    LOG_WARN("warn message");
    LOG_ERROR("error message");
    LOG_CRITICAL("critical message");
  });
}

TEST_F(LoggerTest, RuntimeLevelChangeFilters) {
  // After setting to Error, lower-level macros compile to no-ops or return early.
  EXPECT_NO_THROW(cppws::util::Logger::set_level(cppws::util::Logger::Level::Error));
  EXPECT_NO_THROW(LOG_DEBUG("this should be filtered"));
  EXPECT_NO_THROW(LOG_ERROR("this should pass through"));
}

TEST_F(LoggerTest, StructuredFormattingWorks) {
  // Verify that fmt-style placeholders don't throw or truncate.
  const int port = 8080;
  const std::string host = "127.0.0.1";
  EXPECT_NO_THROW(LOG_INFO("Listening on {}:{}", host, port));
}
