#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// HttpRequest — parsed representation of an HTTP/1.1 request
//
// Memory model:
//   Method, path, and header values are stored as std::string (owned copies).
//   Phase 5 will profile whether string_view into the socket buffer saves
//   enough allocations to justify the lifetime complexity.
//
// HTTP/1.1 wire format:
//   METHOD SP Request-URI SP HTTP-Version CRLF
//   Header: Value CRLF
//   ...
//   CRLF
//   [body]
// ─────────────────────────────────────────────────────────────────────────────

#include <string>
#include <unordered_map>
#include <optional>

namespace cppws::http {

enum class Method : std::uint8_t {
  GET, HEAD, POST, PUT, DELETE, OPTIONS, PATCH, UNKNOWN
};

[[nodiscard]] std::string_view method_to_string(Method method) noexcept;
[[nodiscard]] Method string_to_method(std::string_view str) noexcept;

struct HttpRequest {
  Method method{Method::UNKNOWN};
  std::string path;
  std::string version; // "HTTP/1.1"

  // Headers stored case-insensitively (lowercased on parse).
  // unordered_map with string keys: O(1) lookup, acceptable for ≤ 50 headers.
  // A flat_map would be faster for small N — Phase 5 candidate.
  std::unordered_map<std::string, std::string> headers;

  std::string body;

  // ── Convenience accessors ───────────────────────────────────────────────────

  [[nodiscard]] std::optional<std::string_view> header(std::string_view name) const;

  // Returns true if the request explicitly asks for keep-alive.
  // HTTP/1.1 default: keep-alive. HTTP/1.0 default: close.
  [[nodiscard]] bool wants_keep_alive() const noexcept;

  // Content-Length header value, or 0 if absent.
  [[nodiscard]] std::size_t content_length() const noexcept;
};

} // namespace cppws::http
