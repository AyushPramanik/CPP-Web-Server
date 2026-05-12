#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// HttpParser — incremental HTTP/1.1 request parser
//
// State machine overview:
//
//   IDLE ──▶ REQUEST_LINE ──▶ HEADERS ──▶ BODY ──▶ COMPLETE
//                                │
//                         (empty line)
//                                │
//                           (no body)
//                                ▼
//                            COMPLETE
//
// Incremental design:
//   feed(data) can be called multiple times with partial data.
//   Each call returns:
//     - Incomplete: need more data (hold state, return)
//     - Complete:   full request parsed, result() is valid
//     - Error:      malformed request, send 400 and close
//
// Security limits:
//   - Max request line length: 8 KiB  (mitigates URI-Too-Long attacks)
//   - Max header count: 100           (mitigates header injection / DoS)
//   - Max header size: 8 KiB per field
//   - Max body size: 8 MiB            (configurable via set_max_body_size)
//
// These match Apache's defaults and are in the same range as NGINX.
// ─────────────────────────────────────────────────────────────────────────────

#include "http/request.hpp"

#include <cstddef>
#include <string>
#include <string_view>

namespace cppws::http {

class HttpParser {
public:
  enum class Result : std::uint8_t { Incomplete, Complete, Error };

  static constexpr std::size_t MAX_REQUEST_LINE = 8 * 1024;
  static constexpr std::size_t MAX_HEADER_SIZE  = 8 * 1024;
  static constexpr std::size_t MAX_HEADER_COUNT = 100;
  static constexpr std::size_t MAX_BODY_SIZE    = 8 * 1024 * 1024; // 8 MiB

  HttpParser() = default;

  // Feed more bytes into the parser. May be called repeatedly with partial data.
  // Returns Complete when a full request is available via result().
  [[nodiscard]] Result feed(std::string_view data);

  // Valid only when feed() returned Complete.
  [[nodiscard]] const HttpRequest& result() const noexcept { return request_; }
  [[nodiscard]] HttpRequest take_result() noexcept { return std::move(request_); }

  // Error message populated when feed() returns Error.
  [[nodiscard]] const std::string& error() const noexcept { return error_; }

  // Reset parser state for connection reuse (keep-alive pipeline).
  void reset();

  void set_max_body_size(std::size_t size) noexcept { max_body_size_ = size; }

private:
  enum class State {
    RequestLine,
    Headers,
    Body,
    Complete,
  };

  State state_{State::RequestLine};
  HttpRequest request_{};

  std::string line_buf_{};   // Accumulates partial lines across feed() calls
  std::string error_{};

  std::size_t body_remaining_{0};
  std::size_t max_body_size_{MAX_BODY_SIZE};

  // ── Per-state parsers ───────────────────────────────────────────────────────

  // Returns: number of bytes consumed, or -1 on error (sets error_).
  [[nodiscard]] Result parse_request_line(std::string_view line);
  [[nodiscard]] Result parse_header_line(std::string_view line);
  [[nodiscard]] Result parse_body(std::string_view& remaining);

  // Extract next CRLF-terminated line from data into line_buf_.
  // Returns: bytes consumed (including CRLF), or 0 if no complete line yet.
  [[nodiscard]] std::size_t consume_line(std::string_view data);

  void set_error(std::string msg);
};

} // namespace cppws::http
