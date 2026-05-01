cmake_minimum_required(VERSION 3.22)

project(
  cppws
  VERSION 0.1.0
  DESCRIPTION "Production-grade high-performance HTTP web server in C++20"
  LANGUAGES CXX
)

# ── Language standard ─────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Build type defaults ───────────────────────────────────────────────────────
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

# ── Compiler flags ────────────────────────────────────────────────────────────
# -Wall -Wextra: broad warning coverage
# -Wpedantic: strict ISO compliance
# -Wconversion: catch implicit narrowing
# -Wshadow: prevent shadowed variable bugs
# -fno-omit-frame-pointer: keep stack frames for profiling/perf
add_compile_options(
  -Wall
  -Wextra
  -Wpedantic
  -Wconversion
  -Wshadow
  -Wno-unused-parameter
  -fno-omit-frame-pointer
)

add_compile_options(
  "$<$<CONFIG:Debug>:-O0;-g3;-fsanitize=address,undefined>"
  "$<$<CONFIG:RelWithDebInfo>:-O2;-g>"
  "$<$<CONFIG:Release>:-O3;-DNDEBUG;-march=native>"
)

add_link_options(
  "$<$<CONFIG:Debug>:-fsanitize=address,undefined>"
)

# ── Export compile commands for clang-tidy / IDE ──────────────────────────────
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Dependencies via FetchContent ─────────────────────────────────────────────
include(FetchContent)

# spdlog: fast structured logging (header-only, uses fmt internally)
FetchContent_Declare(
  spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG        v1.14.1
  GIT_SHALLOW    TRUE
)

# GoogleTest: unit and integration testing framework
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.14.0
  GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(spdlog googletest)

# ── Core library (all server logic lives here) ────────────────────────────────
# Compiled as a static library so both the server binary and test binaries
# can link against it without duplicating TUs.
add_library(cppws_core STATIC)

target_include_directories(cppws_core
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(cppws_core
  PUBLIC
    spdlog::spdlog
)

# Platform: Linux epoll is our primary I/O multiplexer
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_compile_definitions(cppws_core PUBLIC CPPWS_PLATFORM_LINUX)
endif()

add_subdirectory(src)

# ── Server executable ─────────────────────────────────────────────────────────
add_executable(cppws src/main.cpp)
target_link_libraries(cppws PRIVATE cppws_core)

# ── Tests ─────────────────────────────────────────────────────────────────────
enable_testing()
add_subdirectory(tests)

# ── Benchmarks ────────────────────────────────────────────────────────────────
add_subdirectory(bench)

# ── Install rules ─────────────────────────────────────────────────────────────
install(TARGETS cppws RUNTIME DESTINATION bin)
install(DIRECTORY include/ DESTINATION include)
