#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// PendingWrite — worker → event loop response delivery
//
// Carries either:
//   A) headers + body string (small responses, error pages)
//   B) headers string + file_fd/file_size for sendfile (static files)
//
// file_fd is -1 for case A. In case B the event loop calls:
//   conn->enqueue_write(headers)
//   conn->begin_sendfile(file_fd, file_size)
//
// Ownership: file_fd is transferred to Connection::begin_sendfile which
// wraps it in a SendfileOp RAII handle. The file_fd must NOT be closed here.
// ─────────────────────────────────────────────────────────────────────────────

#include "net/connection.hpp"

#include <string>

namespace cppws::core {

struct PendingWrite {
  net::ConnectionPtr conn{};
  std::string data{};     // serialized headers (+ body if file_fd == -1)
  int file_fd{-1};        // -1 = body is in `data`; else use sendfile
  off_t file_size{0};
};

} // namespace cppws::core
