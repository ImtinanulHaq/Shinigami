##############################################################################
# services/CMakeLists.txt — top-level build for all four platform services
#
# Usage:
#   cmake -B build -DCMAKE_BUILD_TYPE=Debug
#   cmake --build build
#   ctest --test-dir build
##############################################################################

cmake_minimum_required(VERSION 3.16)
project(platform_services C)

# ── language standard ────────────────────────────────────────────────────
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ── global compiler warnings (all targets inherit from here) ─────────────
add_compile_options(
    -Wall
    -Wextra
    -Werror
    -Wshadow
    -Wformat=2
    -Wno-unused-parameter   # relax for stub implementations still in progress
)

# ── common include directories ───────────────────────────────────────────
include_directories(
    ${CMAKE_SOURCE_DIR}/common
    ${CMAKE_SOURCE_DIR}/../dev/hal/interface
    ${CMAKE_SOURCE_DIR}/../dev/security/core
)

# ── shared service-common library ────────────────────────────────────────
#    All four services link this.  Tests also link it via add_subdirectory.
add_library(service_common STATIC
    common/service_base.c
    common/service_ipc.c
    common/service_config.c
)
target_include_directories(service_common PUBLIC common)

# ── service sub-directories ──────────────────────────────────────────────
add_subdirectory(audio_service)
add_subdirectory(camera_service)
add_subdirectory(sensor_service)
add_subdirectory(gpio_service)

# ── test suite ───────────────────────────────────────────────────────────
enable_testing()
add_subdirectory(testing)
