#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// PendingWrite — worker → event loop response delivery
//
// Workers produce HTTP responses as strings.  They cannot write directly to
// the socket because:
//   1. Only the event loop thread owns the epoll fd and the write-ready state.
//   2. Concurrent writes from multiple threads require a mutex per socket,
//      adding lock overhead on every response.
//
// Instead: workers push a PendingWrite to a thread-safe queue, then write to
// an eventfd.  The event loop wakes on the eventfd, drains the queue, and
// performs the actual socket writes.
//
// This is the same "post back to the reactor" pattern used by libuv (uv_async)
// and Boost.Asio (io_context::post()).
// ─────────────────────────────────────────────────────────────────────────────

#include "net/connection.hpp"

#include <string>

namespace cppws::core {

struct PendingWrite {
  net::ConnectionPtr conn{};  // target connection (shared_ptr keeps it alive)
  std::string data{};         // serialized HTTP response
};

} // namespace cppws::core
