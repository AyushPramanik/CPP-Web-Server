#include "http/static_handler.hpp"

#include "http/mime.hpp"
#include "util/logger.hpp"

#include <fcntl.h>
#include <sys/stat.h>

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
  // Open the file for reading. O_RDONLY | O_CLOEXEC.
  // sendfile(2) requires a file descriptor, not a FILE* — use open() directly.
  const int file_fd = ::open(abs_path.c_str(), O_RDONLY | O_CLOEXEC);
  if (file_fd < 0) {
    LOG_WARN("open({}) failed: {}", abs_path.string(), std::strerror(errno));
    resp = HttpResponse::make_error(Status::InternalServerError);
    return;
  }

  // Stat for file size — needed for Content-Length header before sendfile.
  struct ::stat sb{};
  if (::fstat(file_fd, &sb) < 0) {
    ::close(file_fd);
    resp = HttpResponse::make_error(Status::InternalServerError);
    return;
  }
  const auto file_size = static_cast<std::size_t>(sb.st_size);

  const auto ext  = abs_path.extension().string();
  const auto mime = mime_type(ext);

  LOG_DEBUG("Serving file via sendfile: {} ({} bytes, {})",
            abs_path.string(), file_size, mime);

  resp.set_status(Status::Ok);
  // set_sendfile() takes ownership of file_fd — do not close it here.
  resp.set_sendfile(file_fd, file_size, mime);
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
