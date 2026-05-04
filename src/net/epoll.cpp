#include "net/epoll.hpp"

#include "util/logger.hpp"

#include <cstring>
#include <stdexcept>

#include <sys/epoll.h>

namespace cppws::net {

namespace {

[[noreturn]] void throw_errno(const char* ctx) {
  throw std::runtime_error(std::string(ctx) + ": " + std::strerror(errno));
}

// Build the epoll_event struct with the fd encoded in the data union.
// We use .fd rather than .ptr because it avoids the need to keep a map
// from pointer → fd for cleanup — the fd IS the key.
epoll_event make_epoll_event(int watched_fd, uint32_t events) noexcept {
  epoll_event ev{};
  ev.events = events;
  ev.data.fd = watched_fd;
  return ev;
}

} // namespace

// ── Construction ──────────────────────────────────────────────────────────────

Epoll::Epoll(int max_events) : events_(static_cast<std::size_t>(max_events)) {
  // EPOLL_CLOEXEC: close the epoll fd on exec() so child processes
  // (spawned by CGI handlers, etc.) don't inherit it.
  epfd_ = ::epoll_create1(EPOLL_CLOEXEC);
  if (epfd_ < 0) {
    throw_errno("epoll_create1");
  }
  LOG_DEBUG("epoll instance created, epfd={}", epfd_);
}

Epoll::Epoll(Epoll&& other) noexcept
    : epfd_(other.epfd_), events_(std::move(other.events_)), n_ready_(other.n_ready_) {
  other.epfd_ = -1;
  other.n_ready_ = 0;
}

Epoll& Epoll::operator=(Epoll&& other) noexcept {
  if (this != &other) {
    if (epfd_ >= 0) {
      ::close(epfd_);
    }
    epfd_ = other.epfd_;
    events_ = std::move(other.events_);
    n_ready_ = other.n_ready_;
    other.epfd_ = -1;
    other.n_ready_ = 0;
  }
  return *this;
}

Epoll::~Epoll() {
  if (epfd_ >= 0) {
    ::close(epfd_);
    epfd_ = -1;
  }
}

// ── fd registration ───────────────────────────────────────────────────────────

void Epoll::add(int watched_fd, uint32_t events) {
  auto ev = make_epoll_event(watched_fd, events);
  if (::epoll_ctl(epfd_, EPOLL_CTL_ADD, watched_fd, &ev) < 0) {
    throw_errno("epoll_ctl(ADD)");
  }
}

void Epoll::modify(int watched_fd, uint32_t events) {
  auto ev = make_epoll_event(watched_fd, events);
  if (::epoll_ctl(epfd_, EPOLL_CTL_MOD, watched_fd, &ev) < 0) {
    throw_errno("epoll_ctl(MOD)");
  }
}

void Epoll::remove(int watched_fd) {
  // Kernel >= 2.6.9: the event pointer is ignored for EPOLL_CTL_DEL,
  // but older kernels require a non-null pointer — pass a dummy.
  epoll_event dummy{};
  if (::epoll_ctl(epfd_, EPOLL_CTL_DEL, watched_fd, &dummy) < 0) {
    if (errno != EBADF && errno != ENOENT) {
      // EBADF: fd was already closed (benign during shutdown)
      // ENOENT: fd was never registered (programming error, but non-fatal)
      LOG_WARN("epoll_ctl(DEL) fd={}: {}", watched_fd, std::strerror(errno));
    }
  }
}

// ── Waiting ───────────────────────────────────────────────────────────────────

std::span<const EpollEvent> Epoll::wait(int timeout_ms) {
  // epoll_wait writes into the raw epoll_event array. We reinterpret our
  // EpollEvent vector directly — the layout is identical because EpollEvent
  // contains {uint32_t events, int fd} which matches epoll_event's
  // {uint32_t events, epoll_data_t data} when data.fd is used.
  //
  // We cannot use reinterpret_cast on the vector directly due to strict
  // aliasing, so we use a temporary array and copy the ready events in.
  static thread_local std::vector<epoll_event> raw_events;
  if (raw_events.size() < events_.size()) {
    raw_events.resize(events_.size());
  }

  int n = ::epoll_wait(
      epfd_,
      raw_events.data(),
      static_cast<int>(raw_events.size()),
      timeout_ms);

  if (n < 0) {
    if (errno == EINTR) {
      // Interrupted by a signal (SIGINT, SIGTERM, etc.) — not an error.
      // Return empty span; caller's loop will check the shutdown flag.
      n_ready_ = 0;
      return {};
    }
    throw_errno("epoll_wait");
  }

  n_ready_ = n;

  // Translate raw epoll_events into our EpollEvent structs
  for (int i = 0; i < n; ++i) {
    events_[static_cast<std::size_t>(i)] = EpollEvent{
        .events = raw_events[static_cast<std::size_t>(i)].events,
        .fd     = raw_events[static_cast<std::size_t>(i)].data.fd,
    };
  }

  return std::span<const EpollEvent>(events_.data(), static_cast<std::size_t>(n));
}

} // namespace cppws::net
