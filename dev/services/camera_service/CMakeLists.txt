# =============================================================================
# dev/services/camera_service/CMakeLists.txt
# Camera service daemon — V4L2 video capture over io_uring.
#
# Sources:
#   camera_service_main.c      daemon lifecycle, double-fork, CLI
#   camera_service_hal.c       V4L2 device open/start/stop, MMAP buffer queue
#   camera_service_loop.c      io_uring: IO_OP_POLL on V4L2 fd + 1s timeout
#   camera_service_security.c  security_manager_default_config("camera")
#
# Link graph:
#   camera_service
#     ├── mw::svc_common   (service_base/config/ipc → mw::sm + mw::core)
#     ├── mw::hal_camera   (camera_hal.c — V4L2 kernel headers only, no lib)
#     ├── mw::security     (caps + sandbox + seccomp + verify)
#     ├── mw::core         (io_uring_loop, memory_pool, ring_buffer)
#     └── Threads::Threads
#
# Note: V4L2 is kernel headers only — no library needed. mw::hal_camera
# already carries the right include paths.
# =============================================================================

add_executable(camera_service)

target_sources(camera_service
    PRIVATE
        camera_service_main.c
        camera_service_hal.c
        camera_service_loop.c
        camera_service_security.c
)

target_include_directories(camera_service
    PRIVATE
        # Own service headers (camera_service.h, camera_service_hal.h, etc.)
        ${CMAKE_CURRENT_SOURCE_DIR}
        # common/ headers included via "../common/" in source
        ${CMAKE_CURRENT_SOURCE_DIR}/../common
        # security_manager.h — directly included by camera_service_security.c
        # as "../../dev/security/core/security_manager.h"
        ${CMAKE_SOURCE_DIR}/dev/security/core
        ${CMAKE_SOURCE_DIR}/dev/security
        # Generated version header
        ${CMAKE_BINARY_DIR}/include
)

target_link_libraries(camera_service
    PRIVATE
        mw::svc_common      # service_base + config + ipc (carries mw::sm + mw::core)
        mw::hal_camera      # V4L2 HAL (kernel headers; no runtime lib needed)
        mw::security        # full security stack (caps + sandbox + seccomp + verify)
        mw::core            # explicit: camera_service_loop.c calls io_loop_* directly
        Threads::Threads
)

set_target_properties(camera_service PROPERTIES
    OUTPUT_NAME               "camera_service"
    POSITION_INDEPENDENT_CODE ON
    C_VISIBILITY_PRESET       hidden
)

target_compile_options(camera_service PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-fno-omit-frame-pointer>
)

target_link_options(camera_service PRIVATE
    "-Wl,--as-needed"
    "-Wl,-z,now"
    "-Wl,-z,relro"
    "-Wl,-z,noexecstack"
    "-Wl,--build-id=sha1"
)

install(
    TARGETS camera_service
    RUNTIME
        DESTINATION "${CMAKE_INSTALL_SBINDIR}"
        COMPONENT   runtime
)
