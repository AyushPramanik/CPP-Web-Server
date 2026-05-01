# ─────────────────────────────────────────────────────────────────────────────
# Multi-stage Docker build for cppws
#
# Stage 1 (builder): full toolchain — cmake, clang, spdlog, googletest
# Stage 2 (runtime): only the compiled binary + minimal libc
#
# Build:
#   docker build -t cppws:latest .
#   docker build --target builder -t cppws:dev .  # dev image with tools
#
# Run:
#   docker run --rm -p 8080:8080 cppws:latest
# ─────────────────────────────────────────────────────────────────────────────

# ── Stage 1: builder ──────────────────────────────────────────────────────────
FROM ubuntu:24.04 AS builder

# Avoid interactive prompts from apt
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    ninja-build \
    clang-18 \
    libssl-dev \
    git \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Use clang-18 as default compiler
ENV CC=clang-18
ENV CXX=clang++-18

WORKDIR /src

# Copy everything (FetchContent will pull spdlog + gtest during configure)
COPY . .

# Configure in Release mode for the production image
RUN cmake -B build \
      -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_CXX_COMPILER=clang++-18

RUN cmake --build build --parallel

# Run tests inside builder to fail fast if anything is broken
RUN cd build && ctest --output-on-failure

# ── Stage 2: runtime ──────────────────────────────────────────────────────────
FROM ubuntu:24.04 AS runtime

RUN apt-get update && apt-get install -y --no-install-recommends \
    libssl3 \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Non-root user: production containers must not run as root
RUN useradd --system --create-home --shell /bin/false cppws

WORKDIR /app

COPY --from=builder /src/build/cppws /app/cppws

# Create log directory owned by the cppws user
RUN mkdir -p /app/logs && chown cppws:cppws /app/logs

USER cppws

EXPOSE 8080

# Health check: verify the process is alive (Phase 2 will add HTTP /health)
HEALTHCHECK --interval=30s --timeout=5s --start-period=10s --retries=3 \
    CMD pgrep cppws || exit 1

ENTRYPOINT ["/app/cppws"]
CMD ["--config", "/app/config/server.conf"]
