// ─────────────────────────────────────────────────────────────────────────────
// cppws — bootstrap with thread pool dispatch
//
// HTTP pipeline (Phase 4):
//   Event loop thread:  recv → parser.feed() → request complete?
//     yes → ThreadPool::submit(task)        ← off the event loop thread
//   Worker thread:      router.dispatch()   ← runs HTTP handler
//     → EventLoop::post_response()          ← back to event loop via eventfd
//   Event loop thread:  drain_pending_writes → conn.enqueue_write → flush
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
  if (g_loop != nullptr) g_loop->stop();
}

constexpr uint16_t DEFAULT_PORT    = 8080;
constexpr std::size_t N_WORKERS    = 0; // 0 = hardware_concurrency

struct HttpState {
  cppws::http::HttpParser parser{};
};

cppws::http::Router build_router() {
  cppws::http::Router router;

  router.add_route(cppws::http::Method::GET, "/health",
      [](const cppws::http::HttpRequest&, cppws::http::HttpResponse& resp) {
        resp.set_status(cppws::http::Status::Ok).set_body("ok");
      });

  const std::filesystem::path public_dir =
      std::filesystem::current_path() / "public";

  if (std::filesystem::exists(public_dir)) {
    auto handler = std::make_shared<cppws::http::StaticFileHandler>(public_dir, "/");
    router.add_prefix_route(cppws::http::Method::GET, "/",
        [handler](const cppws::http::HttpRequest& req, cppws::http::HttpResponse& resp) {
          (*handler)(req, resp);
        });
  } else {
    router.add_prefix_route(cppws::http::Method::GET, "/",
        [](const cppws::http::HttpRequest&, cppws::http::HttpResponse& resp) {
          resp.set_status(cppws::http::Status::Ok)
              .set_body("cppws running — create public/ to serve files");
        });
  }

  return router;
}

} // namespace

int main(int /*argc*/, char* /*argv*/[]) {
  cppws::util::Logger::init(cppws::util::Logger::Level::Info, "", true);
  LOG_INFO("cppws v0.1.0 starting on port {}", DEFAULT_PORT);

  try {
    cppws::core::EventLoop loop(DEFAULT_PORT, N_WORKERS);
    g_loop = &loop;

    std::signal(SIGINT,  signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Router is shared (read-only after build) — safe to capture in lambdas
    // accessed by multiple worker threads simultaneously.
    auto router = std::make_shared<cppws::http::Router>(build_router());

    loop.set_connection_handler(
        [&loop, router](cppws::net::ConnectionPtr conn) {
          auto state = std::make_shared<HttpState>();

          conn->set_read_callback(
              [state, &loop, router, weak_conn = std::weak_ptr(conn)](
                  cppws::net::Connection& c) {

                const auto data = c.read_buf().readable();
                const std::string_view sv{data.data(), data.size()};
                const auto result = state->parser.feed(sv);
                c.read_buf().consume_all();

                if (result == cppws::http::HttpParser::Result::Error) {
                  LOG_WARN("Parse error on conn [{}]: {}", c.id(), state->parser.error());
                  auto resp = cppws::http::HttpResponse::make_error(
                      cppws::http::Status::BadRequest, state->parser.error());
                  c.set_keep_alive(false);
                  // Error responses are tiny — send inline without dispatching
                  loop.post_response(weak_conn.lock(), resp.serialize());
                  return;
                }

                if (result == cppws::http::HttpParser::Result::Complete) {
                  auto req = state->parser.take_result();
                  state->parser.reset();

                  const bool keep_alive = req.wants_keep_alive();
                  c.set_keep_alive(keep_alive);

                  // Capture what we need; move req into the task.
                  // weak_ptr prevents the task from keeping the connection alive
                  // past its natural lifetime.
                  auto conn_ptr = weak_conn.lock();
                  if (!conn_ptr) return;

                  loop.submit(
                      [router, req = std::move(req), conn_ptr, &loop, keep_alive]() mutable {
                        cppws::http::HttpResponse resp;
                        if (!router->dispatch(req, resp)) {
                          resp = cppws::http::HttpResponse::make_error(
                              cppws::http::Status::NotFound);
                        }
                        resp.set_header("connection", keep_alive ? "keep-alive" : "close");
                        loop.post_response(conn_ptr, resp.serialize());
                      });
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

  LOG_INFO("cppws stopped");
  cppws::util::Logger::shutdown();
  return EXIT_SUCCESS;
}
