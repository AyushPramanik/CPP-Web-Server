#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// Router — maps HTTP method + path to a handler function
//
// Matching rules (in priority order):
//   1. Exact match:  route("/api/health", GET, handler)  matches "/api/health"
//   2. Prefix match: route("/static/", GET, handler)     matches "/static/*"
//
// Handler signature:
//   void(const HttpRequest&, HttpResponse&)
//
// Design decision: linear scan over a small std::vector.
// For ≤ 100 routes this beats a hash map due to cache locality.
// A radix trie (like httprouter in Go) would be appropriate for larger APIs.
// ─────────────────────────────────────────────────────────────────────────────

#include "http/request.hpp"
#include "http/response.hpp"

#include <functional>
#include <string>
#include <vector>

namespace cppws::http {

using Handler = std::function<void(const HttpRequest&, HttpResponse&)>;

class Router {
public:
  // Register an exact-match route.
  void add_route(Method method, std::string path, Handler handler);

  // Register a prefix-match route (path must end with '/').
  void add_prefix_route(Method method, std::string prefix, Handler handler);

  // Dispatch a request. Fills response and returns true if a route matched.
  // Returns false (→ 404) if no route matches.
  [[nodiscard]] bool dispatch(const HttpRequest& req, HttpResponse& resp) const;

private:
  struct Route {
    Method method;
    std::string path;
    Handler handler;
    bool is_prefix{false};
  };

  std::vector<Route> routes_;
};

} // namespace cppws::http
