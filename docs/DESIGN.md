# cppws — Design Document

> Living document — updated with each phase. This is the authoritative record of
> architectural decisions, tradeoffs, and systems concepts explored in this project.

---

## Project Goals

Build a learning-oriented, production-quality HTTP/1.1 web server that teaches
deep systems programming concepts used in real-world servers (NGINX, Caddy, H2O).

Primary goals:
- Low-latency non-blocking I/O using Linux epoll
- Clean modular architecture (each subsystem has one responsibility)
- Production-style C++20 (RAII, move semantics, no raw owning pointers)
- Minimal allocations in hot paths
- Correct handling of partial reads/writes, connection lifecycle, backpressure

---

## High-Level Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                         cppws process                           │
│                                                                 │
│  ┌──────────────┐    ┌─────────────────────────────────────┐   │
│  │  Main Thread │    │         Worker Thread Pool          │   │
│  │              │    │  ┌──────────┐  ┌──────────┐         │   │
│  │  EventLoop   │───▶│  │ Worker 0 │  │ Worker 1 │  ...    │   │
│  │  (epoll)     │    │  └──────────┘  └──────────┘         │   │
│  │              │    └─────────────────────────────────────┘   │
│  └──────────────┘                    │                         │
│         │                            ▼                         │
│         │                   ┌─────────────────┐               │
│         │                   │  HTTP Layer      │               │
│         │                   │  - Parser        │               │
│         │                   │  - Router        │               │
│         │                   │  - Handler       │               │
│         │                   └─────────────────┘               │
│         │                            │                         │
│         ▼                            ▼                         │
│  ┌──────────────┐          ┌─────────────────┐                │
│  │  Connection  │          │  Middleware      │                │
│  │  Manager     │          │  Pipeline        │                │
│  └──────────────┘          └─────────────────┘                │
└─────────────────────────────────────────────────────────────────┘
```

---

## Phase 1: Project Foundation

**Status:** Complete

### Subsystems built

| Subsystem | File | Purpose |
|-----------|------|---------|
| Logger | `src/util/logger.cpp` | Async structured logging via spdlog |
| Bootstrap | `src/main.cpp` | Signal handling, startup/shutdown sequencing |
| CMake | `CMakeLists.txt` | Build system, dependency management |
| CI | `.github/workflows/ci.yml` | Automated build, test, format, lint |
| Docker | `Dockerfile` | Reproducible build + minimal runtime image |

### Key design decisions

**Async logging:** Log I/O must never block the event loop. We use spdlog's
async logger backed by a SPSC queue and a dedicated writer thread. This is the
same model NGINX uses (write to buffer, flush from writer).

**Static library:** Server logic is compiled into `libcppws_core.a` which both
the server binary and test binaries link. This avoids compiling the same TUs
twice and ensures tests exercise the exact same code that ships.

**Signal handling:** We install `signal_handler` which sets an atomic bool.
The event loop checks this flag at the top of each iteration. We never call
non-async-signal-safe functions (malloc, spdlog) from the handler itself.

---

## Phase 2: Networking Core (planned)

### Concepts to implement

**Non-blocking sockets:** `fcntl(fd, F_SETFL, O_NONBLOCK)` makes `read()`/`write()`
return `EAGAIN` instead of blocking when no data is ready. This is fundamental
to handling thousands of connections in a single thread.

**epoll (Linux):** The kernel's O(1) I/O readiness notification mechanism.
Unlike `select`/`poll` which scan a list every call, epoll uses a red-black
tree internally and delivers only ready events. NGINX defaults to epoll on
Linux.

**Edge-triggered vs level-triggered:**
- Level-triggered (LT): epoll wakes you up as long as data is available.
  Simpler but can cause unnecessary wakeups.
- Edge-triggered (ET): epoll wakes you up *once* when state changes (no data
  → data available). More efficient but requires reading until `EAGAIN`.

We will use edge-triggered mode (`EPOLLET`) like NGINX does.

**Reactor pattern:** One thread owns the event loop. When epoll says a
connection is readable, we do the minimal work (read into buffer, parse
header), then dispatch to a worker thread for handler execution. This separates
I/O from CPU work.

```
Kernel ──(epoll_wait returns)──▶ EventLoop::run_once()
                                       │
                     ┌─────────────────┴──────────────────┐
                     │                                    │
             (EPOLLIN on fd)                    (EPOLLOUT on fd)
                     │                                    │
              conn.read_into_buf()              conn.flush_write_buf()
                     │
              parser.feed(buf)
                     │
              [request complete?]
                     │ yes
              task_queue.push(handle_request)
```

**Partial reads/writes:** TCP is a stream protocol. A single `send()` call may
transmit fewer bytes than requested. The write path must loop until all bytes
are sent or `EAGAIN` is returned (in which case we re-register for `EPOLLOUT`).

---

## Event Loop Design

```
loop:
  events = epoll_wait(epfd, events[], max_events, timeout_ms)
  for each event:
    if event.fd == listen_fd:
      accept_new_connection()
    else if event.events & EPOLLIN:
      connection.on_readable()
    else if event.events & EPOLLOUT:
      connection.on_writable()
    else if event.events & (EPOLLHUP | EPOLLERR):
      connection.close()
  check_timers()
  check_shutdown_flag()
```

---

## Thread Model

```
Main thread:              epoll_wait → accept → dispatch to task queue
Worker threads (N):       dequeue task → call handler → write response buffer
Logger thread (1):        drain log queue → write to file/stdout
```

`N` workers = `std::thread::hardware_concurrency()` by default.
Workers communicate with the main thread via a lock-free MPSC task queue.

---

## Performance Philosophy

1. **Avoid copies in hot path.** Request parsing uses a view (`std::string_view`)
   into the socket buffer — no heap allocation per header field.

2. **Minimize syscalls.** Use `sendfile(2)` for static files — copies data
   directly from page cache to socket without userspace involvement.

3. **Buffer pooling.** Each connection gets a fixed-size read buffer from a pool.
   No `malloc` per connection. Inspired by NGINX's `ngx_pool_t`.

4. **Edge-triggered epoll.** Fewer epoll notifications means fewer context
   switches. We pay for it with mandatory drain loops.

5. **TCP_CORK / TCP_NODELAY.** For small responses we set `TCP_NODELAY` to avoid
   Nagle's algorithm delay. For large streaming responses we `TCP_CORK` to
   batch segments.

---

## Comparison to NGINX

| Concept | NGINX | cppws |
|---------|-------|-------|
| I/O model | epoll ET | epoll ET |
| Worker processes | Multiple (fork) | Single process, thread pool |
| Memory | `ngx_pool_t` custom allocator | Buffer pool + arena (Phase 5) |
| Config | nginx.conf (custom DSL) | TOML-like (Phase 3) |
| Logging | Custom non-blocking | spdlog async |
| Static files | `sendfile` | `sendfile` (Phase 5) |
| TLS | via OpenSSL/BoringSSL | OpenSSL (Phase 6) |
