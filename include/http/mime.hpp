#pragma once

#include <string_view>

namespace cppws::http {

// Returns the MIME Content-Type for a file extension (with leading dot).
// Falls back to "application/octet-stream" for unknown types.
// Example: mime_type(".html") → "text/html; charset=utf-8"
[[nodiscard]] std::string_view mime_type(std::string_view extension) noexcept;

} // namespace cppws::http
