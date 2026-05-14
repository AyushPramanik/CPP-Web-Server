#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// StaticFileHandler — serves files from a root directory
//
// Security:
//   Path traversal is prevented by resolving the canonical path and verifying
//   it starts with the configured root directory. A request to
//   "/static/../../../etc/passwd" becomes an absolute path that does NOT start
//   with the root, so it's rejected with 403.
//
// Performance (Phase 5 will add):
//   - sendfile(2): zero-copy from page cache to socket
//   - If-Modified-Since / ETag caching
//   - Range requests for partial content (video streaming)
//
// For now: read the file into memory and enqueue via Connection::enqueue_write.
// This is correct but not optimal for large files — Phase 5 fixes that.
// ─────────────────────────────────────────────────────────────────────────────

#include "http/request.hpp"
#include "http/response.hpp"

#include <filesystem>
#include <string>

namespace cppws::http {

class StaticFileHandler {
public:
  // root_dir: absolute path to the directory to serve files from.
  // strip_prefix: URL prefix to strip before mapping to filesystem.
  //   E.g., root="/var/www", strip="/static/" → GET /static/a.html → /var/www/a.html
  explicit StaticFileHandler(std::filesystem::path root_dir,
                             std::string strip_prefix = "/");

  // Handler-compatible signature for use with Router::add_prefix_route.
  void operator()(const HttpRequest& req, HttpResponse& resp) const;

  // Serve a specific file path (used by operator() after path resolution).
  void serve_file(const std::filesystem::path& abs_path, HttpResponse& resp) const;

private:
  std::filesystem::path root_dir_;
  std::string strip_prefix_;

  // Resolve URL path to filesystem path, checking for traversal.
  // Returns empty path if the resolved path escapes root_dir_.
  [[nodiscard]] std::filesystem::path resolve(std::string_view url_path) const;
};

} // namespace cppws::http
