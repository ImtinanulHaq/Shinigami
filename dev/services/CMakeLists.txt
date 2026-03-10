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
    -Wno-unused-parameter   # relax for stub implementations still in progre
    -Wno-stringop-truncation  # suppress false positives on strncpy(buf, src, sizeof(buf)-1)
    -Wno-unused-result        # suppress write()/read() return value warningsss
)

# ── common include directories ───────────────────────────────────────────
include_directories(
    ${CMAKE_SOURCE_DIR}/common
    ${CMAKE_SOURCE_DIR}/../hal/interface
    ${CMAKE_SOURCE_DIR}/../hal/layers/audio
    ${CMAKE_SOURCE_DIR}/../hal/layers/camera
    ${CMAKE_SOURCE_DIR}/../hal/layers/gpio
    ${CMAKE_SOURCE_DIR}/../hal/layers/sensors
    ${CMAKE_SOURCE_DIR}/../core
    ${CMAKE_SOURCE_DIR}/../core/service_manager/infrastructure
    ${CMAKE_SOURCE_DIR}/../security/verify
    ${CMAKE_SOURCE_DIR}/../security/core
)

# ── shared service-common library ────────────────────────────────────────
#    All four services link this.  Tests also link it via add_subdirectory.
add_library(service_common STATIC
    common/service_base.c
    common/service_ipc.c
    common/service_config.c
)
target_include_directories(service_common PUBLIC common)

# ── verify library (HMAC-based message authentication) ───────────────────
add_library(verify_lib STATIC
    ${CMAKE_SOURCE_DIR}/../security/verify/verify.c
    ${CMAKE_SOURCE_DIR}/../security/verify/verify_hmac.c
    ${CMAKE_SOURCE_DIR}/../security/verify/verify_replay.c
    ${CMAKE_SOURCE_DIR}/../security/verify/verify_token.c
)
target_include_directories(verify_lib PUBLIC
    ${CMAKE_SOURCE_DIR}/../security/verify
    ${CMAKE_SOURCE_DIR}/../core/service_manager/security)
# sm_crypto_lib provides sm_hmac_* symbols used by verify_hmac.c
# (forward-declared here; CMake resolves link order automatically)
target_link_libraries(verify_lib PUBLIC sm_crypto_lib)

# ── HAL interface + device layer libraries ────────────────────────────────
add_library(hal_interface_lib STATIC
    ${CMAKE_SOURCE_DIR}/../hal/interface/hal_interface.c
)
target_include_directories(hal_interface_lib PUBLIC
    ${CMAKE_SOURCE_DIR}/../hal/interface)

add_library(hal_audio_lib STATIC
    ${CMAKE_SOURCE_DIR}/../hal/layers/audio/audio_hal.c
)
target_include_directories(hal_audio_lib PUBLIC
    ${CMAKE_SOURCE_DIR}/../hal/layers/audio
    ${CMAKE_SOURCE_DIR}/../hal/interface)
target_link_libraries(hal_audio_lib PUBLIC hal_interface_lib)

add_library(hal_camera_lib STATIC
    ${CMAKE_SOURCE_DIR}/../hal/layers/camera/camera_hal.c
)
target_include_directories(hal_camera_lib PUBLIC
    ${CMAKE_SOURCE_DIR}/../hal/layers/camera
    ${CMAKE_SOURCE_DIR}/../hal/interface)
target_link_libraries(hal_camera_lib PUBLIC hal_interface_lib)

add_library(hal_sensor_lib STATIC
    ${CMAKE_SOURCE_DIR}/../hal/layers/sensors/sensor_hal.c
)
target_include_directories(hal_sensor_lib PUBLIC
    ${CMAKE_SOURCE_DIR}/../hal/layers/sensors
    ${CMAKE_SOURCE_DIR}/../hal/interface)
target_link_libraries(hal_sensor_lib PUBLIC hal_interface_lib)

add_library(hal_gpio_lib STATIC
    ${CMAKE_SOURCE_DIR}/../hal/layers/gpio/gpio_hal.c
)
target_include_directories(hal_gpio_lib PUBLIC
    ${CMAKE_SOURCE_DIR}/../hal/layers/gpio
    ${CMAKE_SOURCE_DIR}/../hal/interface)
target_link_libraries(hal_gpio_lib PUBLIC hal_interface_lib)

# ── ring buffer library ───────────────────────────────────────────────────
add_library(ring_buffer_lib STATIC
    ${CMAKE_SOURCE_DIR}/../core/ring_buffer.c
)
target_include_directories(ring_buffer_lib PUBLIC
    ${CMAKE_SOURCE_DIR}/../core)

# ── memory pool library ─────────────────────────────────────────────────
add_library(memory_pool_lib STATIC
    ${CMAKE_SOURCE_DIR}/../core/memory_pool.c
)
target_include_directories(memory_pool_lib PUBLIC
    ${CMAKE_SOURCE_DIR}/../core)
target_link_libraries(memory_pool_lib PRIVATE pthread)

# ── io_uring event loop library ─────────────────────────────────────────
add_library(io_uring_loop_lib STATIC
    ${CMAKE_SOURCE_DIR}/../core/io_uring_loop.c
)
target_include_directories(io_uring_loop_lib PUBLIC
    ${CMAKE_SOURCE_DIR}/../core)
target_link_libraries(io_uring_loop_lib PUBLIC memory_pool_lib uring)

# ── SM crypto library (HMAC primitives used by verify_lib) ───────────────
add_library(sm_crypto_lib STATIC
    ${CMAKE_SOURCE_DIR}/../core/service_manager/security/sm_crypto.c
    ${CMAKE_SOURCE_DIR}/../core/service_manager/observability/sm_logging.c
)
target_include_directories(sm_crypto_lib PUBLIC
    ${CMAKE_SOURCE_DIR}/../core/service_manager/security
    ${CMAKE_SOURCE_DIR}/../core/service_manager/observability)
target_link_libraries(sm_crypto_lib PUBLIC ssl crypto)

# ── security manager library ──────────────────────────────────────────────
add_library(security_manager_lib STATIC
    ${CMAKE_SOURCE_DIR}/../security/core/security_manager.c
    ${CMAKE_SOURCE_DIR}/../security/capabilities/capabilities.c
    ${CMAKE_SOURCE_DIR}/../security/capabilities/capabilities_audit.c
    ${CMAKE_SOURCE_DIR}/../security/capabilities/capabilities_core.c
    ${CMAKE_SOURCE_DIR}/../security/capabilities/capabilities_policy.c
    ${CMAKE_SOURCE_DIR}/../security/sandbox/sandbox.c
    ${CMAKE_SOURCE_DIR}/../security/sandbox/sandbox_cgroup.c
    ${CMAKE_SOURCE_DIR}/../security/sandbox/sandbox_core.c
    ${CMAKE_SOURCE_DIR}/../security/sandbox/sandbox_mount.c
    ${CMAKE_SOURCE_DIR}/../security/sandbox/sandbox_network.c
    ${CMAKE_SOURCE_DIR}/../security/seccomp/seccomp_core.c
    ${CMAKE_SOURCE_DIR}/../security/seccomp/seccomp_filter.c
    ${CMAKE_SOURCE_DIR}/../security/seccomp/seccomp_policy_audio.c
    ${CMAKE_SOURCE_DIR}/../security/seccomp/seccomp_policy_camera.c
    ${CMAKE_SOURCE_DIR}/../security/seccomp/seccomp_policy_minimal.c
    ${CMAKE_SOURCE_DIR}/../security/seccomp/seccomp_policy_network.c
    ${CMAKE_SOURCE_DIR}/../security/seccomp/seccomp_policy_sensor.c
)
target_include_directories(security_manager_lib PUBLIC
    ${CMAKE_SOURCE_DIR}/../security/core
    ${CMAKE_SOURCE_DIR}/../security/capabilities
    ${CMAKE_SOURCE_DIR}/../security/sandbox
    ${CMAKE_SOURCE_DIR}/../security/seccomp
    ${CMAKE_SOURCE_DIR}/../security/verify)
# Allow GCC format-truncation warnings in the external security sources
target_compile_options(security_manager_lib PRIVATE -Wno-format-truncation)
target_link_libraries(security_manager_lib PUBLIC seccomp)

# ── service sub-directories ──────────────────────────────────────────────
add_subdirectory(audio_service)
add_subdirectory(camera_service)
add_subdirectory(sensor_service)
add_subdirectory(gpio_service)

# ── test suite ───────────────────────────────────────────────────────────
enable_testing()
add_subdirectory(testing)
