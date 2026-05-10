#include "http/response.hpp"

#include <format>
#include <sstream>

namespace cppws::http {

std::string_view status_to_reason(Status code) noexcept {
  switch (code) {
    case Status::Ok:                  return "OK";
    case Status::Created:             return "Created";
    case Status::NoContent:           return "No Content";
    case Status::MovedPermanently:    return "Moved Permanently";
    case Status::NotModified:         return "Not Modified";
    case Status::BadRequest:          return "Bad Request";
    case Status::Forbidden:           return "Forbidden";
    case Status::NotFound:            return "Not Found";
    case Status::MethodNotAllowed:    return "Method Not Allowed";
    case Status::RequestTimeout:      return "Request Timeout";
    case Status::PayloadTooLarge:     return "Payload Too Large";
    case Status::UriTooLong:          return "URI Too Long";
    case Status::InternalServerError: return "Internal Server Error";
    case Status::NotImplemented:      return "Not Implemented";
    case Status::ServiceUnavailable:  return "Service Unavailable";
  }
  return "Unknown";
}

HttpResponse& HttpResponse::set_body(std::string body_str, std::string_view content_type) {
  body = std::move(body_str);
  headers["content-type"] = std::string(content_type);
  return *this;
}

HttpResponse& HttpResponse::set_header(std::string name, std::string value) {
  // Store header names lowercased for consistency with request parsing.
  std::string lower_name(name.size(), '\0');
  std::transform(name.begin(), name.end(), lower_name.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  headers[std::move(lower_name)] = std::move(value);
  return *this;
}

std::string HttpResponse::serialize() const {
  // Pre-size the string to avoid repeated reallocs.
  // Status line ~30 bytes + each header ~40 bytes + body.
  std::string out;
  out.reserve(128 + headers.size() * 40 + body.size());

  // Status line
  out += "HTTP/1.1 ";
  out += std::to_string(static_cast<int>(status));
  out += ' ';
  out += status_to_reason(status);
  out += "\r\n";

  // Headers
  bool has_content_length = headers.count("content-length") > 0;
  for (const auto& [name, value] : headers) {
    out += name;
    out += ": ";
    out += value;
    out += "\r\n";
  }

  // Auto content-length if body is present and header is absent
  if (!body.empty() && !has_content_length) {
    out += "content-length: ";
    out += std::to_string(body.size());
    out += "\r\n";
  }

  out += "\r\n";
  out += body;
  return out;
}

HttpResponse HttpResponse::make_error(Status code, std::string_view detail) {
  HttpResponse resp;
  resp.status = code;
  const auto reason = status_to_reason(code);
  const int code_int = static_cast<int>(code);
  std::string body_str;
  body_str.reserve(64 + detail.size());
  body_str += std::to_string(code_int);
  body_str += ' ';
  body_str += reason;
  if (!detail.empty()) {
    body_str += '\n';
    body_str += detail;
  }
  resp.set_body(std::move(body_str), "text/plain");
  return resp;
}

HttpResponse HttpResponse::make_redirect(std::string_view location) {
  HttpResponse resp;
  resp.status = Status::MovedPermanently;
  resp.headers["location"] = std::string(location);
  return resp;
}

} // namespace cppws::http
