#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// Epoll — edge-triggered I/O event notification wrapper
//
// How epoll works (kernel internals simplified):
//   1. epoll_create1() allocates a kernel data structure (red-black tree of
//      watched fds + a ready list).
//   2. epoll_ctl(EPOLL_CTL_ADD) registers an fd in the tree.
//   3. epoll_wait() blocks until at least one fd is ready, then moves those
//      entries to the ready list and copies them to userspace.
//
// Level-triggered (default):
//   epoll_wait returns as long as the fd has unread data.
//   Simpler — no mandatory drain loop — but more wakeups under load.
//
// Edge-triggered (EPOLLET):
//   epoll_wait returns ONCE when state transitions from "not ready" → "ready".
//   Fewer wakeups → fewer context switches → better throughput.
//   Mandatory: after each notification, read/write in a loop until EAGAIN.
//
// We use edge-triggered, same as NGINX. This is correct but harder to get
// right — every read path must handle partial reads properly.
// ─────────────────────────────────────────────────────────────────────────────

#include <cstdint>
#include <span>
#include <vector>

#include <sys/epoll.h>

namespace cppws::net {

// ─────────────────────────────────────────────────────────────────────────────
// EpollEvent — thin wrapper around epoll_event to hide the raw C struct
// ─────────────────────────────────────────────────────────────────────────────
struct EpollEvent {
  uint32_t events{0};  // EPOLLIN | EPOLLOUT | EPOLLET | EPOLLRDHUP etc.
  int fd{-1};          // fd that became ready

  [[nodiscard]] bool is_readable()  const noexcept { return events & EPOLLIN; }
  [[nodiscard]] bool is_writable()  const noexcept { return events & EPOLLOUT; }
  [[nodiscard]] bool is_hangup()    const noexcept { return events & (EPOLLHUP | EPOLLRDHUP); }
  [[nodiscard]] bool is_error()     const noexcept { return events & EPOLLERR; }
  [[nodiscard]] bool is_closed()    const noexcept { return is_hangup() || is_error(); }
};

// ─────────────────────────────────────────────────────────────────────────────
// Epoll — RAII wrapper around an epoll instance
//
// Usage:
//   Epoll ep;
//   ep.add(listen_fd, EPOLLIN | EPOLLET);
//   while (running) {
//     auto events = ep.wait(timeout_ms);
//     for (auto& ev : events) { ... }
//   }
// ─────────────────────────────────────────────────────────────────────────────
class Epoll {
public:
  // max_events: upper bound on events returned per epoll_wait call.
  // 1024 is a common default — large enough to batch events under load,
  // small enough that stack allocation stays reasonable.
  static constexpr int DEFAULT_MAX_EVENTS = 1024;

  explicit Epoll(int max_events = DEFAULT_MAX_EVENTS);

  Epoll(const Epoll&) = delete;
  Epoll& operator=(const Epoll&) = delete;

  Epoll(Epoll&& other) noexcept;
  Epoll& operator=(Epoll&& other) noexcept;

  ~Epoll();

  // ── fd registration ─────────────────────────────────────────────────────────

  // Register fd. events: EPOLLIN | EPOLLOUT | EPOLLET | EPOLLRDHUP, etc.
  // EPOLLRDHUP: detect peer half-close without an extra read() call.
  // We always include EPOLLET for edge-triggered mode.
  void add(int watched_fd, uint32_t events);

  // Modify the event mask for an already-registered fd.
  // Common use: add EPOLLOUT after a partial write, remove it once the buffer
  // is drained (to avoid spurious wakeups when the socket is always writable).
  void modify(int watched_fd, uint32_t events);

  // Remove fd from epoll. Must be called before closing the fd, otherwise
  // the kernel holds a dangling reference until the fd is garbage-collected.
  void remove(int watched_fd);

  // ── Waiting ─────────────────────────────────────────────────────────────────

  // Block until events are ready or timeout_ms elapses.
  // timeout_ms = -1  → block indefinitely
  // timeout_ms =  0  → return immediately (poll mode)
  // Returns a span over the internal events buffer — valid until next wait().
  [[nodiscard]] std::span<const EpollEvent> wait(int timeout_ms = -1);

  [[nodiscard]] int epfd() const noexcept { return epfd_; }
  [[nodiscard]] bool is_valid() const noexcept { return epfd_ != -1; }

private:
  int epfd_{-1};
  std::vector<EpollEvent> events_;  // reused across wait() calls — zero alloc in steady state
  int n_ready_{0};                  // number of events returned by last wait()
};

} // namespace cppws::net
