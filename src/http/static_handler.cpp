#include "http/static_handler.hpp"

#include "http/mime.hpp"
#include "util/logger.hpp"

#include <fstream>
#include <iterator>

namespace cppws::http {

StaticFileHandler::StaticFileHandler(std::filesystem::path root_dir,
                                     std::string strip_prefix)
    : root_dir_(std::filesystem::canonical(root_dir)),
      strip_prefix_(std::move(strip_prefix)) {}

void StaticFileHandler::operator()(const HttpRequest& req, HttpResponse& resp) const {
  const auto abs_path = resolve(req.path);

  if (abs_path.empty()) {
    resp = HttpResponse::make_error(Status::Forbidden, "access denied");
    return;
  }

  if (!std::filesystem::exists(abs_path)) {
    resp = HttpResponse::make_error(Status::NotFound);
    return;
  }

  // Directory index: try index.html
  std::filesystem::path target = abs_path;
  if (std::filesystem::is_directory(abs_path)) {
    target = abs_path / "index.html";
    if (!std::filesystem::exists(target)) {
      resp = HttpResponse::make_error(Status::NotFound, "no index.html");
      return;
    }
  }

  if (!std::filesystem::is_regular_file(target)) {
    resp = HttpResponse::make_error(Status::Forbidden, "not a regular file");
    return;
  }

  serve_file(target, resp);
}

void StaticFileHandler::serve_file(const std::filesystem::path& abs_path,
                                   HttpResponse& resp) const {
  // Read entire file into memory.
  // Phase 5 will replace this with sendfile(2) for zero-copy.
  std::ifstream file(abs_path, std::ios::binary);
  if (!file) {
    LOG_WARN("Failed to open file: {}", abs_path.string());
    resp = HttpResponse::make_error(Status::InternalServerError);
    return;
  }

  std::string content(std::istreambuf_iterator<char>(file), {});
  if (file.fail() && !file.eof()) {
    resp = HttpResponse::make_error(Status::InternalServerError, "read error");
    return;
  }

  // Determine MIME type from extension
  const auto ext  = abs_path.extension().string();
  const auto mime = mime_type(ext);

  LOG_DEBUG("Serving file: {} ({} bytes, {})", abs_path.string(), content.size(), mime);

  resp.set_status(Status::Ok);
  resp.set_body(std::move(content), mime);
}

std::filesystem::path StaticFileHandler::resolve(std::string_view url_path) const {
  // Strip URL prefix to get the relative file path
  std::string_view rel = url_path;
  if (rel.starts_with(strip_prefix_)) {
    rel.remove_prefix(strip_prefix_.size());
  }

  // Build candidate absolute path (lexically — no filesystem access yet)
  auto candidate = (root_dir_ / rel).lexically_normal();

  // Security check: canonical path must start with root_dir_.
  // lexically_normal() resolves ".." components without hitting the filesystem.
  // We also call canonical() to resolve symlinks — a symlink outside root is
  // still a traversal attack.
  std::error_code ec;
  const auto real = std::filesystem::weakly_canonical(candidate, ec);
  if (ec) {
    LOG_DEBUG("Cannot resolve path {}: {}", candidate.string(), ec.message());
    return {};
  }

  // Verify the resolved path is within root
  const auto [root_end, real_end] = std::mismatch(
      root_dir_.begin(), root_dir_.end(), real.begin(), real.end());
  (void)real_end;

  if (root_end != root_dir_.end()) {
    LOG_WARN("Path traversal attempt: url={} resolved={}", url_path, real.string());
    return {};
  }

  return real;
}

} // namespace cppws::http
