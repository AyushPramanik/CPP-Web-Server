#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// HttpResponse — builds the raw bytes written to the socket
//
// Design: builder pattern. The caller sets status/headers/body, then calls
// serialize() which returns the full wire-format response as a string.
// The string is then passed to Connection::enqueue_write().
//
// Performance note: serialize() does one allocation. For static file serving
// we skip serialize() entirely and use sendfile() (Phase 5).
// ─────────────────────────────────────────────────────────────────────────────

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

namespace cppws::http {

// Common HTTP status codes — add more as needed.
enum class Status : std::uint16_t {
  Ok                  = 200,
  Created             = 201,
  NoContent           = 204,
  MovedPermanently    = 301,
  NotModified         = 304,
  BadRequest          = 400,
  Forbidden           = 403,
  NotFound            = 404,
  MethodNotAllowed    = 405,
  RequestTimeout      = 408,
  PayloadTooLarge     = 413,
  UriTooLong          = 414,
  InternalServerError = 500,
  NotImplemented      = 501,
  ServiceUnavailable  = 503,
};

[[nodiscard]] std::string_view status_to_reason(Status code) noexcept;

struct HttpResponse {
  Status status{Status::Ok};
  std::unordered_map<std::string, std::string> headers;
  std::string body;

  // ── Builder methods ─────────────────────────────────────────────────────────

  HttpResponse& set_status(Status code) { status = code; return *this; }
  HttpResponse& set_body(std::string body_str, std::string_view content_type = "text/plain");
  HttpResponse& set_header(std::string name, std::string value);

  // ── Serialization ───────────────────────────────────────────────────────────

  // Returns the complete HTTP/1.1 response as a string ready to send.
  // Automatically sets Content-Length if body is present and header is absent.
  [[nodiscard]] std::string serialize() const;

  // ── Zero-copy sendfile support ──────────────────────────────────────────────

  // Set a file descriptor as the response body source (Phase 5 optimization).
  // The response owns file_fd — it will be closed after the transfer.
  // serialize() generates headers only (no body) when has_sendfile() is true.
  HttpResponse& set_sendfile(int file_fd, std::size_t file_size,
                             std::string_view content_type);

  [[nodiscard]] bool has_sendfile() const noexcept { return sendfile_fd_ >= 0; }
  [[nodiscard]] int  sendfile_fd()   const noexcept { return sendfile_fd_; }
  [[nodiscard]] std::size_t sendfile_size() const noexcept { return sendfile_size_; }

  // Transfer fd ownership out — called by the dispatch layer.
  // After this, has_sendfile() returns false.
  [[nodiscard]] int take_sendfile_fd() noexcept {
    const int saved = sendfile_fd_;
    sendfile_fd_ = -1;
    return saved;
  }

  // ── Static factory helpers ──────────────────────────────────────────────────

  [[nodiscard]] static HttpResponse make_error(Status code, std::string_view detail = "");
  [[nodiscard]] static HttpResponse make_redirect(std::string_view location);

private:
  int sendfile_fd_{-1};
  std::size_t sendfile_size_{0};
};

} // namespace cppws::http
