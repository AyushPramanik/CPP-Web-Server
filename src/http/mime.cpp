#include "http/mime.hpp"

#include <array>
#include <algorithm>

namespace cppws::http {

namespace {

// Sorted array of {extension, mime-type} pairs for binary search.
// Sorted so we can use std::lower_bound for O(log N) lookup —
// no hash table needed for a static table this small.
struct MimeEntry {
  std::string_view ext{};
  std::string_view type{};
};

// Extensions are lowercased; callers must lowercase before lookup.
constexpr std::array<MimeEntry, 42> MIME_TABLE{{
  {".aac",   "audio/aac"},
  {".avif",  "image/avif"},
  {".bmp",   "image/bmp"},
  {".css",   "text/css; charset=utf-8"},
  {".csv",   "text/csv"},
  {".eot",   "application/vnd.ms-fontobject"},
  {".flac",  "audio/flac"},
  {".gif",   "image/gif"},
  {".gz",    "application/gzip"},
  {".htm",   "text/html; charset=utf-8"},
  {".html",  "text/html; charset=utf-8"},
  {".ico",   "image/x-icon"},
  {".ics",   "text/calendar"},
  {".jpg",   "image/jpeg"},
  {".jpeg",  "image/jpeg"},
  {".js",    "text/javascript; charset=utf-8"},
  {".json",  "application/json"},
  {".m3u8",  "application/x-mpegURL"},
  {".map",   "application/json"},
  {".mp3",   "audio/mpeg"},
  {".mp4",   "video/mp4"},
  {".ogg",   "audio/ogg"},
  {".ogv",   "video/ogg"},
  {".otf",   "font/otf"},
  {".pdf",   "application/pdf"},
  {".png",   "image/png"},
  {".rss",   "application/rss+xml"},
  {".svg",   "image/svg+xml"},
  {".tar",   "application/x-tar"},
  {".tiff",  "image/tiff"},
  {".ts",    "video/mp2t"},
  {".ttf",   "font/ttf"},
  {".txt",   "text/plain; charset=utf-8"},
  {".wasm",  "application/wasm"},
  {".webm",  "video/webm"},
  {".webp",  "image/webp"},
  {".woff",  "font/woff"},
  {".woff2", "font/woff2"},
  {".xhtml", "application/xhtml+xml"},
  {".xml",   "text/xml"},
  {".zip",   "application/zip"},
  {".zst",   "application/zstd"},
}};

} // namespace

std::string_view mime_type(std::string_view extension) noexcept {
  // Binary search requires sorted input — MIME_TABLE is sorted by ext.
  auto found = std::lower_bound(
      MIME_TABLE.begin(), MIME_TABLE.end(), extension,
      [](const MimeEntry& entry, std::string_view key) {
        return entry.ext < key;
      });

  if (found != MIME_TABLE.end() && found->ext == extension) {
    return found->type;
  }
  return "application/octet-stream";
}

} // namespace cppws::http
