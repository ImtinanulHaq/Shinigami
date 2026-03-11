# =============================================================================
# dev/services/CMakeLists.txt
# Hardware service daemons — top-level dispatcher.
#
# Build order enforced by CMake dependency graph (not directory order):
#
#   mw::core  →  mw::hal_{audio,camera,gpio,sensors}
#             →  mw::sm  (SM client API)
#             →  mw::security
#             →  mw::svc_common
#             →  {audio,camera,gpio,sensor}_service  (executables)
#
# Each daemon shares the same anatomy:
#   <svc>_main.c       — daemon lifecycle, double-fork, CLI parsing
#   <svc>_hal.c        — HAL bring-up for this device
#   <svc>_loop.c       — io_uring event loop (IO_OP_POLL + periodic timeout)
#   <svc>_security.c   — applies security_manager_default_config("<svc>")
#
# HAL-gated: each daemon is only built if its HAL layer is enabled.
# This lets you ship a headless sensor-only image, for example, without
# compiling in audio/camera/gpio dead code.
# =============================================================================

if(NOT MW_BUILD_SERVICES)
    message(STATUS "[MW] Services: skipped (MW_BUILD_SERVICES=OFF)")
    return()
endif()

# Shared service infrastructure — must come first, all daemons depend on it
add_subdirectory(common)

# Per-device daemons — only built when the matching HAL layer is enabled
if(MW_HAL_AUDIO)
    add_subdirectory(audio_service)
    message(STATUS "[MW] Services: audio_service enabled")
else()
    message(STATUS "[MW] Services: audio_service skipped (MW_HAL_AUDIO=OFF)")
endif()

if(MW_HAL_CAMERA)
    add_subdirectory(camera_service)
    message(STATUS "[MW] Services: camera_service enabled")
else()
    message(STATUS "[MW] Services: camera_service skipped (MW_HAL_CAMERA=OFF)")
endif()

if(MW_HAL_GPIO)
    add_subdirectory(gpio_service)
    message(STATUS "[MW] Services: gpio_service enabled")
else()
    message(STATUS "[MW] Services: gpio_service skipped (MW_HAL_GPIO=OFF)")
endif()

if(MW_HAL_SENSORS)
    add_subdirectory(sensor_service)
    message(STATUS "[MW] Services: sensor_service enabled")
else()
    message(STATUS "[MW] Services: sensor_service skipped (MW_HAL_SENSORS=OFF)")
endif()
