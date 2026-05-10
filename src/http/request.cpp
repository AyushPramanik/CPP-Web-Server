#include "http/request.hpp"

#include <algorithm>
#include <charconv>
#include <string>

namespace cppws::http {

std::string_view method_to_string(Method method) noexcept {
  switch (method) {
    case Method::GET:     return "GET";
    case Method::HEAD:    return "HEAD";
    case Method::POST:    return "POST";
    case Method::PUT:     return "PUT";
    case Method::DELETE:  return "DELETE";
    case Method::OPTIONS: return "OPTIONS";
    case Method::PATCH:   return "PATCH";
    case Method::UNKNOWN: return "UNKNOWN";
  }
  return "UNKNOWN";
}

Method string_to_method(std::string_view str) noexcept {
  if (str == "GET")     return Method::GET;
  if (str == "HEAD")    return Method::HEAD;
  if (str == "POST")    return Method::POST;
  if (str == "PUT")     return Method::PUT;
  if (str == "DELETE")  return Method::DELETE;
  if (str == "OPTIONS") return Method::OPTIONS;
  if (str == "PATCH")   return Method::PATCH;
  return Method::UNKNOWN;
}

std::optional<std::string_view> HttpRequest::header(std::string_view name) const {
  // Headers are stored lowercased; normalize the lookup key too.
  std::string lower_name(name);
  std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

  auto it = headers.find(lower_name);
  if (it == headers.end()) {
    return std::nullopt;
  }
  return std::string_view{it->second};
}

bool HttpRequest::wants_keep_alive() const noexcept {
  auto conn = header("connection");
  if (version == "HTTP/1.1") {
    // HTTP/1.1: keep-alive by default, unless "Connection: close"
    if (conn && *conn == "close") return false;
    return true;
  }
  // HTTP/1.0: close by default, unless "Connection: keep-alive"
  if (conn && *conn == "keep-alive") return true;
  return false;
}

std::size_t HttpRequest::content_length() const noexcept {
  auto val = header("content-length");
  if (!val) return 0;
  std::size_t result = 0;
  // std::from_chars avoids locale-sensitive parsing (no allocation)
  auto [ptr, ec] = std::from_chars(val->data(), val->data() + val->size(), result);
  if (ec != std::errc{}) return 0;
  return result;
}

} // namespace cppws::http
