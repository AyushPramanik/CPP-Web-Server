// ─────────────────────────────────────────────────────────────────────────────
// cppws — bootstrap
//
// HTTP pipeline per connection:
//   recv bytes → feed parser → parse complete?
//     yes → router.dispatch(req, resp) → resp.serialize() → enqueue_write
//     no  → wait for more data (keep-alive preserves parser state)
// ─────────────────────────────────────────────────────────────────────────────

#include "core/event_loop.hpp"
#include "http/parser.hpp"
#include "http/response.hpp"
#include "http/router.hpp"
#include "http/static_handler.hpp"
#include "util/logger.hpp"

#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <memory>

namespace {

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
cppws::core::EventLoop* g_loop = nullptr;

extern "C" void signal_handler(int /*signum*/) {
  if (g_loop != nullptr) {
    g_loop->stop();
  }
}

constexpr uint16_t DEFAULT_PORT = 8080;

// ── Per-connection HTTP state ─────────────────────────────────────────────────
// Each Connection gets its own Parser instance (incremental, keeps state
// across partial reads). We store it in a shared_ptr alongside the connection.
struct HttpState {
  cppws::http::HttpParser parser{};
};

// Build the router with all application routes.
cppws::http::Router build_router() {
  cppws::http::Router router;

  // Health-check endpoint — useful for load balancer probes
  router.add_route(cppws::http::Method::GET, "/health",
      [](const cppws::http::HttpRequest& /*req*/, cppws::http::HttpResponse& resp) {
        resp.set_status(cppws::http::Status::Ok).set_body("ok");
      });

  // Static files served from ./public/ (relative to CWD)
  // In production this would be configured via a config file.
  const std::filesystem::path public_dir =
      std::filesystem::current_path() / "public";
  if (std::filesystem::exists(public_dir)) {
    auto static_handler = std::make_shared<cppws::http::StaticFileHandler>(
        public_dir, "/");
    router.add_prefix_route(cppws::http::Method::GET, "/",
        [static_handler](const cppws::http::HttpRequest& req,
                         cppws::http::HttpResponse& resp) {
          (*static_handler)(req, resp);
        });
  } else {
    // Fallback if no public/ directory exists
    router.add_prefix_route(cppws::http::Method::GET, "/",
        [](const cppws::http::HttpRequest& /*req*/,
           cppws::http::HttpResponse& resp) {
          resp.set_status(cppws::http::Status::Ok).set_body(
              "cppws is running. Create a public/ directory to serve files.",
              "text/plain");
        });
  }

  return router;
}

} // namespace

int main(int /*argc*/, char* /*argv*/[]) {
  cppws::util::Logger::init(cppws::util::Logger::Level::Info, "", true);
  LOG_INFO("cppws starting (v0.1.0) on port {}", DEFAULT_PORT);

  try {
    cppws::core::EventLoop loop(DEFAULT_PORT);
    g_loop = &loop;

    std::signal(SIGINT,  signal_handler);
    std::signal(SIGTERM, signal_handler);

    auto router = build_router();

    // Install the HTTP pipeline as the connection handler.
    // Each accepted connection gets a shared HttpState (parser per connection).
    loop.set_connection_handler(
        [&router](cppws::net::ConnectionPtr conn) {
          auto state = std::make_shared<HttpState>();

          conn->set_read_callback(
              [state, &router](cppws::net::Connection& c) {
                // Feed all available bytes to the parser
                const auto data = c.read_buf().readable();
                const std::string_view sv{data.data(), data.size()};
                const auto result = state->parser.feed(sv);
                c.read_buf().consume_all();

                if (result == cppws::http::HttpParser::Result::Error) {
                  LOG_WARN("HTTP parse error on conn [{}]: {}",
                           c.id(), state->parser.error());
                  auto resp = cppws::http::HttpResponse::make_error(
                      cppws::http::Status::BadRequest, state->parser.error());
                  c.set_keep_alive(false);
                  c.enqueue_write(resp.serialize());
                  c.flush_write();
                  return;
                }

                if (result == cppws::http::HttpParser::Result::Complete) {
                  auto req = state->parser.take_result();
                  state->parser.reset();

                  cppws::http::HttpResponse resp;
                  if (!router.dispatch(req, resp)) {
                    resp = cppws::http::HttpResponse::make_error(
                        cppws::http::Status::NotFound);
                  }

                  // Honour keep-alive from the request
                  const bool keep_alive = req.wants_keep_alive();
                  c.set_keep_alive(keep_alive);
                  resp.set_header("connection", keep_alive ? "keep-alive" : "close");

                  c.enqueue_write(resp.serialize());
                  c.flush_write();
                }
              });
        });

    loop.run();
    g_loop = nullptr;

  } catch (const std::exception& ex) {
    LOG_CRITICAL("Fatal: {}", ex.what());
    cppws::util::Logger::shutdown();
    return EXIT_FAILURE;
  }

  LOG_INFO("cppws stopped cleanly");
  cppws::util::Logger::shutdown();
  return EXIT_SUCCESS;
}
