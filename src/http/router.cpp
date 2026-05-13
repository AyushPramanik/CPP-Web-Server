#include "http/router.hpp"

#include "util/logger.hpp"

namespace cppws::http {

void Router::add_route(Method method, std::string path, Handler handler) {
  routes_.push_back(Route{method, std::move(path), std::move(handler), false});
}

void Router::add_prefix_route(Method method, std::string prefix, Handler handler) {
  routes_.push_back(Route{method, std::move(prefix), std::move(handler), true});
}

bool Router::dispatch(const HttpRequest& req, HttpResponse& resp) const {
  // Two-pass: exact routes first, then prefix routes.
  // This ensures "/static/index.html" exact route beats "/static/" prefix.

  // Pass 1: exact match
  for (const auto& route : routes_) {
    if (!route.is_prefix && route.method == req.method && route.path == req.path) {
      LOG_TRACE("Router: exact match {} {}", method_to_string(req.method), req.path);
      route.handler(req, resp);
      return true;
    }
  }

  // Pass 2: prefix match (longest prefix wins)
  const Route* best = nullptr;
  for (const auto& route : routes_) {
    if (route.is_prefix && route.method == req.method &&
        req.path.starts_with(route.path)) {
      if (best == nullptr || route.path.size() > best->path.size()) {
        best = &route;
      }
    }
  }

  if (best != nullptr) {
    LOG_TRACE("Router: prefix match {} {}", method_to_string(req.method), req.path);
    best->handler(req, resp);
    return true;
  }

  LOG_DEBUG("Router: no match for {} {}", method_to_string(req.method), req.path);
  return false;
}

} // namespace cppws::http
