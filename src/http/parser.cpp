#include "http/parser.hpp"

#include "util/logger.hpp"

#include <algorithm>
#include <cctype>

namespace cppws::http {

namespace {

// Trim leading and trailing whitespace (SP, HT) from a string_view.
// Used for header value normalization per RFC 7230 §3.2.6.
std::string_view trim(std::string_view sv) noexcept {
  while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t')) sv.remove_prefix(1);
  while (!sv.empty() && (sv.back()  == ' ' || sv.back()  == '\t')) sv.remove_suffix(1);
  return sv;
}

// Lowercase a string in-place for case-insensitive header name storage.
void to_lower(std::string& str) noexcept {
  for (char& ch : str) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
}

} // namespace

// ── Public API ────────────────────────────────────────────────────────────────

HttpParser::Result HttpParser::feed(std::string_view data) {
  // Walk through the input, consuming lines (REQUEST_LINE and HEADERS states)
  // or raw bytes (BODY state) until we run out of data or hit an error/complete.
  while (!data.empty()) {
    if (state_ == State::Body) {
      const Result res = parse_body(data);
      if (res != Result::Incomplete) return res;
      break; // consumed all available body bytes, wait for more
    }

    // For RequestLine and Headers states: consume one CRLF-delimited line
    const std::size_t consumed = consume_line(data);
    if (consumed == 0) {
      break; // No complete line yet — partial read, wait for more data
    }
    data.remove_prefix(consumed);

    // line_buf_ now holds the line without the trailing CRLF
    const std::string_view line{line_buf_};

    Result res = Result::Incomplete;
    if (state_ == State::RequestLine) {
      res = parse_request_line(line);
    } else if (state_ == State::Headers) {
      res = parse_header_line(line);
    }

    line_buf_.clear();

    if (res == Result::Error || res == Result::Complete) {
      return res;
    }
  }

  return state_ == State::Complete ? Result::Complete : Result::Incomplete;
}

void HttpParser::reset() {
  state_ = State::RequestLine;
  request_ = HttpRequest{};
  line_buf_.clear();
  body_remaining_ = 0;
  error_.clear();
}

// ── Private parsers ───────────────────────────────────────────────────────────

std::size_t HttpParser::consume_line(std::string_view data) {
  // Look for \r\n (CRLF) — required by RFC 7230.
  // We also accept bare \n for leniency with broken clients.
  const auto pos = data.find('\n');
  if (pos == std::string_view::npos) {
    // No newline yet — accumulate in line_buf_ and wait for more data
    if (line_buf_.size() + data.size() > MAX_HEADER_SIZE) {
      set_error("header line too long");
      return 0;
    }
    line_buf_ += data;
    return data.size(); // All consumed, but no complete line
  }

  // Found newline at pos. Include everything up to and including it.
  const std::size_t consumed = pos + 1;
  const std::string_view chunk = data.substr(0, consumed);

  if (line_buf_.size() + chunk.size() > MAX_HEADER_SIZE) {
    set_error("header line too long");
    return 0;
  }
  line_buf_ += chunk;

  // Strip trailing \r\n or \n from line_buf_
  if (!line_buf_.empty() && line_buf_.back() == '\n') line_buf_.pop_back();
  if (!line_buf_.empty() && line_buf_.back() == '\r') line_buf_.pop_back();

  return consumed;
}

HttpParser::Result HttpParser::parse_request_line(std::string_view line) {
  // Format: METHOD SP Request-URI SP HTTP-Version
  // Example: GET /index.html HTTP/1.1
  if (line.size() > MAX_REQUEST_LINE) {
    set_error("request line too long");
    return Result::Error;
  }

  const auto sp1 = line.find(' ');
  if (sp1 == std::string_view::npos) {
    set_error("malformed request line: missing first SP");
    return Result::Error;
  }

  const auto sp2 = line.find(' ', sp1 + 1);
  if (sp2 == std::string_view::npos) {
    set_error("malformed request line: missing second SP");
    return Result::Error;
  }

  request_.method  = string_to_method(line.substr(0, sp1));
  request_.path    = std::string(line.substr(sp1 + 1, sp2 - sp1 - 1));
  request_.version = std::string(line.substr(sp2 + 1));

  if (request_.method == Method::UNKNOWN) {
    set_error("unknown HTTP method");
    return Result::Error;
  }

  if (request_.version != "HTTP/1.1" && request_.version != "HTTP/1.0") {
    set_error("unsupported HTTP version: " + request_.version);
    return Result::Error;
  }

  state_ = State::Headers;
  return Result::Incomplete;
}

HttpParser::Result HttpParser::parse_header_line(std::string_view line) {
  // Empty line signals end of headers
  if (line.empty()) {
    // Transition based on whether we expect a body
    body_remaining_ = request_.content_length();
    if (body_remaining_ > 0) {
      if (body_remaining_ > max_body_size_) {
        set_error("body too large");
        return Result::Error;
      }
      request_.body.reserve(body_remaining_);
      state_ = State::Body;
    } else {
      state_ = State::Complete;
      return Result::Complete;
    }
    return Result::Incomplete;
  }

  if (request_.headers.size() >= MAX_HEADER_COUNT) {
    set_error("too many headers");
    return Result::Error;
  }

  // Format: Name: Value
  // RFC 7230: field-name is case-insensitive, field-value may have optional
  // leading/trailing whitespace.
  const auto colon = line.find(':');
  if (colon == std::string_view::npos) {
    set_error("malformed header: no colon");
    return Result::Error;
  }

  std::string name(line.substr(0, colon));
  to_lower(name); // normalize for case-insensitive lookup
  const std::string value(trim(line.substr(colon + 1)));

  request_.headers[std::move(name)] = value;
  return Result::Incomplete;
}

HttpParser::Result HttpParser::parse_body(std::string_view& remaining) {
  const std::size_t take = std::min(remaining.size(), body_remaining_);
  request_.body.append(remaining.data(), take);
  remaining.remove_prefix(take);
  body_remaining_ -= take;

  if (body_remaining_ == 0) {
    state_ = State::Complete;
    return Result::Complete;
  }
  return Result::Incomplete;
}

void HttpParser::set_error(std::string msg) {
  error_ = std::move(msg);
  state_ = State::Complete; // Halt parsing
  LOG_DEBUG("HTTP parse error: {}", error_);
}

} // namespace cppws::http
