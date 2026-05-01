# cppws — C++ Web Server

A production-grade, high-performance HTTP/1.1 web server written in modern C++20,
inspired by NGINX's architecture. Built as a deep dive into systems programming,
event-driven I/O, and low-latency networking.

> **Purpose:** This is a learning-oriented implementation. Every design decision
> is documented, every systems concept is explained. The goal is code you can
> learn from, not just run.

---

## Architecture Overview

- **Event-driven non-blocking I/O** using Linux `epoll` (edge-triggered)
- **Reactor pattern** — one event loop dispatches to a worker thread pool
- **HTTP/1.1** parser with keep-alive support
- **Static file serving** with `sendfile(2)` (zero-copy)
- **Configurable routing** and middleware pipeline
- **Async structured logging** via spdlog
- **Graceful shutdown** with connection draining

See [docs/DESIGN.md](docs/DESIGN.md) for full architecture documentation.

---

## Build Requirements

| Tool | Version |
|------|---------|
| CMake | ≥ 3.22 |
| C++ compiler | clang-18+ or gcc-14+ |
| Linux kernel | ≥ 5.4 (for epoll, io_uring later) |
| OpenSSL | ≥ 3.0 (for Phase 6 TLS) |

Dependencies (fetched automatically by CMake):
- [spdlog](https://github.com/gabime/spdlog) v1.14.1
- [GoogleTest](https://github.com/google/googletest) v1.14.0

---

## Quick Start

```bash
# Clone
git clone https://github.com/your-org/cppws.git && cd cppws

# Build (Debug + ASan)
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel

# Run tests
cd build && ctest --output-on-failure

# Run server
./build/cppws
```

### Docker

```bash
docker build -t cppws:latest .
docker run --rm -p 8080:8080 cppws:latest
```

---

## Project Structure

```
cppws/
├── include/              # Public headers
│   ├── core/             # EventLoop, Reactor, Server
│   ├── http/             # Parser, Request, Response, Router
│   ├── net/              # Socket, Connection, Buffer
│   ├── util/             # Logger, Error, StringUtils
│   └── config/           # Config parser
├── src/                  # Implementation files (mirrors include/)
├── tests/
│   ├── unit/             # Parser, utility tests (fast, no network)
│   ├── integration/      # Full server tests (real TCP connections)
│   └── load/             # wrk/hey-based load tests
├── bench/                # Micro-benchmarks
├── docs/                 # DESIGN.md, architecture diagrams
├── scripts/              # format.sh, lint.sh, benchmark.sh
└── .github/workflows/    # CI pipeline
```

---

## Development Phases

| Phase | Status | Description |
|-------|--------|-------------|
| 1 | ✅ Complete | Project foundation, logging, CMake, CI, Docker |
| 2 | 🔄 In Progress | Non-blocking sockets, epoll, event loop, connection lifecycle |
| 3 | ⏳ Planned | HTTP parser, routing, static file serving |
| 4 | ⏳ Planned | Thread pool, task queue, synchronization |
| 5 | ⏳ Planned | sendfile, buffer pooling, benchmarks |
| 6 | ⏳ Planned | Reverse proxy, load balancing, TLS |
| 7 | ⏳ Planned | Stress tests, metrics, graceful reload |

---

## Code Quality

```bash
# Format all source files
find src include tests -name '*.cpp' -o -name '*.hpp' \
  | xargs clang-format -i

# Run clang-tidy
clang-tidy -p build src/**/*.cpp

# Run with sanitizers (Debug build)
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build && ./build/cppws
```

---

## License

MIT — see [LICENSE](LICENSE).
