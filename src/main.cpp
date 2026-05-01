// ─────────────────────────────────────────────────────────────────────────────
// cppws — C++ Web Server
//
// Bootstrap sequence mirrors production servers like NGINX:
//   1. Parse config / CLI args
//   2. Initialize logger (before any other subsystem logs)
//   3. Initialize subsystems (event loop, thread pool, HTTP layer)
//   4. Install signal handlers
//   5. Enter event loop (blocks until shutdown signal)
//   6. Graceful drain + cleanup
// ─────────────────────────────────────────────────────────────────────────────

#include "util/logger.hpp"

#include <csignal>
#include <cstdlib>
#include <atomic>

namespace {

// std::atomic<bool> for signal-handler → main-thread communication.
// Signal handlers must only call async-signal-safe functions; setting an
// atomic flag is safe and avoids the pitfalls of longjmp-based approaches.
std::atomic<bool> g_shutdown_requested{false};

extern "C" void signal_handler(int signum) {
  // Write to the atomic; main loop will check and break.
  // We intentionally avoid logging here — spdlog is not async-signal-safe.
  g_shutdown_requested.store(true, std::memory_order_relaxed);
  (void)signum;
}

} // namespace

int main(int argc, char* argv[]) {
  // ── Logger init ─────────────────────────────────────────────────────────────
  // Async mode: background writer thread so I/O threads never block on log I/O.
  // Log file: empty → stdout only for now; Phase 1 config will add file paths.
  cppws::util::Logger::init(cppws::util::Logger::Level::Debug, "", true);

  LOG_INFO("cppws starting up (version {})", "0.1.0");
  LOG_DEBUG("Build type: " CMAKE_BUILD_TYPE);

  // ── Signal handling ─────────────────────────────────────────────────────────
  // SIGINT  → Ctrl-C during development
  // SIGTERM → sent by systemd / Docker / Kubernetes when pod is evicted
  // SIGHUP  → traditionally used for config reload (handled in Phase 6)
  std::signal(SIGINT,  signal_handler);
  std::signal(SIGTERM, signal_handler);

  LOG_INFO("Signal handlers installed (SIGINT, SIGTERM)");

  // ── Placeholder event loop ──────────────────────────────────────────────────
  // Phase 2 will replace this with the real epoll-based event loop.
  // For now we just block until a signal arrives so we can verify the build.
  LOG_INFO("Event loop placeholder — press Ctrl-C to exit");

  while (!g_shutdown_requested.load(std::memory_order_relaxed)) {
    // In Phase 2 this becomes: event_loop.run_once(timeout_ms);
    pause(); // suspend until any signal
  }

  // ── Graceful shutdown ───────────────────────────────────────────────────────
  LOG_INFO("Shutdown signal received — draining connections");
  // Phase 2+: event_loop.stop(); thread_pool.drain_and_join();
  LOG_INFO("cppws stopped cleanly");

  cppws::util::Logger::shutdown();
  return EXIT_SUCCESS;
}
