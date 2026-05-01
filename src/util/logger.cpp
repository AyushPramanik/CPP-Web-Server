#include "util/logger.hpp"

#include <spdlog/async.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace cppws::util {

namespace {

// Map our Level enum to spdlog's enum.  Keeping the mapping local avoids
// leaking spdlog types into the public header.
constexpr spdlog::level::level_enum to_spdlog(Logger::Level l) {
  switch (l) {
    case Logger::Level::Trace:    return spdlog::level::trace;
    case Logger::Level::Debug:    return spdlog::level::debug;
    case Logger::Level::Info:     return spdlog::level::info;
    case Logger::Level::Warn:     return spdlog::level::warn;
    case Logger::Level::Error:    return spdlog::level::err;
    case Logger::Level::Critical: return spdlog::level::critical;
    case Logger::Level::Off:      return spdlog::level::off;
  }
  return spdlog::level::info; // unreachable
}

} // namespace

void Logger::init(Level level, std::string_view log_file, bool async) {
  // Queue size must be a power of two (spdlog requirement).
  // 8192 entries is generous for a typical server — each entry is a
  // pre-formatted string, so memory footprint is bounded.
  constexpr std::size_t queue_size = 8192;
  constexpr std::size_t n_backing_threads = 1; // one writer thread is enough

  std::vector<spdlog::sink_ptr> sinks;

  // Console sink with ANSI colours — critical for developer experience
  auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
  console_sink->set_level(to_spdlog(level));
  sinks.push_back(console_sink);

  // Rotating file sink: 10 MiB per file, keep 5 rotated files.
  // This mirrors NGINX's access_log + logrotate setup.
  if (!log_file.empty()) {
    constexpr std::size_t max_file_size = 10 * 1024 * 1024; // 10 MiB
    constexpr std::size_t max_files = 5;
    auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        std::string(log_file), max_file_size, max_files);
    file_sink->set_level(to_spdlog(level));
    sinks.push_back(file_sink);
  }

  std::shared_ptr<spdlog::logger> logger;

  if (async) {
    spdlog::init_thread_pool(queue_size, n_backing_threads);
    logger = std::make_shared<spdlog::async_logger>(
        "cppws",
        sinks.begin(), sinks.end(),
        spdlog::thread_pool(),
        spdlog::async_overflow_policy::block // backpressure: block if full
    );
  } else {
    logger = std::make_shared<spdlog::logger>(
        "cppws", sinks.begin(), sinks.end());
  }

  // Pattern: [timestamp] [level] [thread] message
  // %Y-%m-%d %H:%M:%S.%e  — millisecond precision timestamp
  // %^%l%$                 — level with colour
  // %t                     — thread id (useful for debugging race conditions)
  // %v                     — the actual message
  logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [t:%t] %v");
  logger->set_level(to_spdlog(level));

  // Flush to disk on WARN or above so errors survive crashes
  logger->flush_on(spdlog::level::warn);

  spdlog::set_default_logger(std::move(logger));
}

void Logger::shutdown() {
  spdlog::shutdown();
}

void Logger::set_level(Level level) {
  spdlog::set_level(to_spdlog(level));
}

} // namespace cppws::util
