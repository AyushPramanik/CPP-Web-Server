// ─────────────────────────────────────────────────────────────────────────────
// cppws — bootstrap sequence
//
// Signal handling strategy (mirrors NGINX's approach):
//   • A raw pointer to the EventLoop is stored at file scope.
//   • The signal handler calls loop->stop(), which only stores to an atomic —
//     that's async-signal-safe (POSIX 7.1.4).
//   • This is the same pattern NGINX uses via ngx_cycle global.
//
// Bootstrap order:
//   1. Initialize logger
//   2. Construct EventLoop (binds port, sets up epoll)
//   3. Store loop pointer for signal handler
//   4. Install signal handlers
//   5. Run event loop (blocks until stop())
//   6. Graceful shutdown + logger flush
// ─────────────────────────────────────────────────────────────────────────────

#include "core/event_loop.hpp"
#include "util/logger.hpp"

#include <csignal>
#include <cstdlib>

namespace {

// Raw non-owning pointer — set before signals are installed, never written again.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
cppws::core::EventLoop* g_loop = nullptr;

extern "C" void signal_handler(int signum) {
  // stop() sets an atomic bool — async-signal-safe.
  if (g_loop != nullptr) {
    g_loop->stop();
  }
  (void)signum;
}

constexpr uint16_t DEFAULT_PORT = 8080;

} // namespace

int main(int /*argc*/, char* /*argv*/[]) {
  cppws::util::Logger::init(cppws::util::Logger::Level::Debug, "", true);
  LOG_INFO("cppws starting (v0.1.0) on port {}", DEFAULT_PORT);

  try {
    cppws::core::EventLoop loop(DEFAULT_PORT);

    // Expose to signal handler before installing signals
    g_loop = &loop;

    std::signal(SIGINT,  signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Placeholder HTTP handler — Phase 3 replaces this with real routing
    loop.set_connection_handler([](cppws::net::ConnectionPtr conn) {
      conn->set_read_callback([](cppws::net::Connection& c) {
        if (!c.read_buf().empty()) {
          static constexpr std::string_view RESPONSE =
              "HTTP/1.1 200 OK\r\n"
              "Content-Length: 13\r\n"
              "Content-Type: text/plain\r\n"
              "Connection: close\r\n"
              "\r\n"
              "Hello, World!";
          c.enqueue_write(RESPONSE);
          c.set_keep_alive(false);
          c.flush_write();
          c.read_buf().consume_all();
        }
      });
    });

    loop.run(); // Blocks until signal_handler calls loop.stop()

    g_loop = nullptr; // Clear before loop destructs

  } catch (const std::exception& ex) {
    LOG_CRITICAL("Fatal: {}", ex.what());
    cppws::util::Logger::shutdown();
    return EXIT_FAILURE;
  }

  LOG_INFO("cppws stopped cleanly");
  cppws::util::Logger::shutdown();
  return EXIT_SUCCESS;
}
