#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// Logging subsystem
//
// Design rationale
// ────────────────
// High-performance servers must NEVER block I/O threads on log I/O. NGINX
// achieves this by writing to an in-memory buffer and flushing from a
// dedicated writer.  We use spdlog's async logger which follows the same
// pattern: a lock-free SPSC queue feeds a background thread that owns the
// file descriptors.
//
// Log levels (from most to least verbose):
//   TRACE → DEBUG → INFO → WARN → ERROR → CRITICAL
//
// In Release builds spdlog compiles away TRACE/DEBUG calls entirely via
// SPDLOG_ACTIVE_LEVEL, giving zero overhead in the hot path.
//
// Usage:
//   Logger::init(Logger::Level::Info, "logs/server.log");
//   LOG_INFO("Listening on port {}", 8080);
//   LOG_DEBUG("Request parsed: method={} path={}", method, path);
// ─────────────────────────────────────────────────────────────────────────────

#include <string_view>

// Set active log level before including spdlog headers so the macros compile
// away below this level.
#ifndef SPDLOG_ACTIVE_LEVEL
  #ifdef NDEBUG
    #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_INFO
  #else
    #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
  #endif
#endif

#include <spdlog/spdlog.h>

// ── Convenience macros ────────────────────────────────────────────────────────
// We wrap spdlog macros so callers are decoupled from the logging backend.
// If we ever replace spdlog, only this header changes.
#define LOG_TRACE(...)    SPDLOG_TRACE(__VA_ARGS__)
#define LOG_DEBUG(...)    SPDLOG_DEBUG(__VA_ARGS__)
#define LOG_INFO(...)     SPDLOG_INFO(__VA_ARGS__)
#define LOG_WARN(...)     SPDLOG_WARN(__VA_ARGS__)
#define LOG_ERROR(...)    SPDLOG_ERROR(__VA_ARGS__)
#define LOG_CRITICAL(...) SPDLOG_CRITICAL(__VA_ARGS__)

namespace cppws::util {

// ─────────────────────────────────────────────────────────────────────────────
// Logger
//
// Singleton initializer for the global spdlog default logger.  Call init()
// once at startup before spawning any threads.  After that, all LOG_* macros
// work from any thread without further setup.
// ─────────────────────────────────────────────────────────────────────────────
class Logger {
public:
  enum class Level { Trace, Debug, Info, Warn, Error, Critical, Off };

  // Initialize the async logger.
  //   level      — minimum log level; messages below are discarded
  //   log_file   — optional file path; empty string → stdout only
  //   async      — true: background writer thread (production default)
  //                false: synchronous (useful in tests / single-threaded tools)
  static void init(Level level = Level::Info,
                   std::string_view log_file = "",
                   bool async = true);

  // Flush all pending log records and shut down the async thread pool.
  // Call during graceful shutdown before process exit.
  static void shutdown();

  // Runtime level change (e.g., SIGHUP-triggered config reload)
  static void set_level(Level level);
};

} // namespace cppws::util
