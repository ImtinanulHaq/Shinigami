# =============================================================================
# dev/services/gpio_service/CMakeLists.txt
# GPIO service daemon — sysfs GPIO pin management over io_uring.
#
# Sources:
#   gpio_service_main.c       daemon lifecycle, double-fork, CLI
#                             Note: one daemon instance manages one GPIO chip;
#                             multiple pins handled within a single process.
#   gpio_service_hal.c        GPIO pin open/configure/read/write via sysfs
#   gpio_service_loop.c       io_uring: IO_OP_POLL on GPIO event fd + 1s timeout
#   gpio_service_security.c   security_manager_default_config("gpio")
#
# Link graph:
#   gpio_service
#     ├── mw::svc_common   (service_base/config/ipc → mw::sm + mw::core)
#     ├── mw::hal_gpio     (gpio_hal.c — sysfs; no runtime lib needed)
#     ├── mw::security     (caps + sandbox + seccomp + verify)
#     ├── mw::core         (io_uring_loop, memory_pool, ring_buffer)
#     └── Threads::Threads
#
# GPIO uses sysfs (/sys/class/gpio/*) — no userspace library dependency.
# The gpio_hal.c file reads/writes sysfs nodes directly. The HAL layer
# may also use the character device interface (/dev/gpiochipN) on kernels
# that support it — still no library, just ioctl calls.
# =============================================================================

add_executable(gpio_service)

target_sources(gpio_service
    PRIVATE
        gpio_service_main.c
        gpio_service_hal.c
        gpio_service_loop.c
        gpio_service_security.c
)

target_include_directories(gpio_service
    PRIVATE
        # Own service headers (gpio_service.h, gpio_service_hal.h, etc.)
        ${CMAKE_CURRENT_SOURCE_DIR}
        # common/ headers included via "../common/" in source
        ${CMAKE_CURRENT_SOURCE_DIR}/../common
        # security_manager.h — directly included by gpio_service_security.c
        # as "../../dev/security/core/security_manager.h"
        ${CMAKE_SOURCE_DIR}/dev/security/core
        ${CMAKE_SOURCE_DIR}/dev/security
        # Generated version header
        ${CMAKE_BINARY_DIR}/include
)

target_link_libraries(gpio_service
    PRIVATE
        mw::svc_common      # service_base + config + ipc (carries mw::sm + mw::core)
        mw::hal_gpio        # sysfs GPIO HAL (no runtime lib; kernel headers only)
        mw::security        # full security stack (caps + sandbox + seccomp + verify)
        mw::core            # explicit: gpio_service_loop.c calls io_loop_* directly
        Threads::Threads
)

set_target_properties(gpio_service PROPERTIES
    OUTPUT_NAME               "gpio_service"
    POSITION_INDEPENDENT_CODE ON
    C_VISIBILITY_PRESET       hidden
)

target_compile_options(gpio_service PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-fno-omit-frame-pointer>
)

target_link_options(gpio_service PRIVATE
    "-Wl,--as-needed"
    "-Wl,-z,now"
    "-Wl,-z,relro"
    "-Wl,-z,noexecstack"
    "-Wl,--build-id=sha1"
)

install(
    TARGETS gpio_service
    RUNTIME
        DESTINATION "${CMAKE_INSTALL_SBINDIR}"
        COMPONENT   runtime
)
