# =============================================================================
# dev/services/sensor_service/CMakeLists.txt
# Sensor service daemon — IIO accelerometer / gyroscope / temperature over io_uring.
#
# Sources:
#   sensor_service_main.c      daemon lifecycle, double-fork, CLI
#   sensor_service_hal.c       IIO device open/configure/read via sysfs/eventfd
#   sensor_service_loop.c      io_uring: IO_OP_POLL on IIO event fd + 1s timeout
#   sensor_service_security.c  security_manager_default_config("sensor")
#
# Link graph:
#   sensor_service
#     ├── mw::svc_common    (service_base/config/ipc → mw::sm + mw::core)
#     ├── mw::hal_sensors   (sensor_hal.c — IIO sysfs; no runtime lib needed)
#     ├── mw::security      (caps + sandbox + seccomp + verify)
#     ├── mw::core          (io_uring_loop, memory_pool, ring_buffer)
#     └── Threads::Threads
#
# IIO (Industrial I/O) uses the kernel sysfs interface — no userspace library.
# sensor_hal.c opens the IIO event fd at /dev/iio:deviceN and feeds it into
# the io_uring IO_OP_POLL path in sensor_service_loop.c.
# The memory_pool is used directly here to pool sensor_reading_t structs
# (defined in memory_pool.h as a convenience type alongside the pool API).
# =============================================================================

add_executable(sensor_service)

target_sources(sensor_service
    PRIVATE
        sensor_service_main.c
        sensor_service_hal.c
        sensor_service_loop.c
        sensor_service_security.c
)

target_include_directories(sensor_service
    PRIVATE
        # Own service headers (sensor_service.h, sensor_service_hal.h, etc.)
        ${CMAKE_CURRENT_SOURCE_DIR}
        # common/ headers included via "../common/" in source
        ${CMAKE_CURRENT_SOURCE_DIR}/../common
        # security_manager.h — directly included by sensor_service_security.c
        # as "../../dev/security/core/security_manager.h"
        ${CMAKE_SOURCE_DIR}/dev/security/core
        ${CMAKE_SOURCE_DIR}/dev/security
        # Generated version header
        ${CMAKE_BINARY_DIR}/include
)

target_link_libraries(sensor_service
    PRIVATE
        mw::svc_common      # service_base + config + ipc (carries mw::sm + mw::core)
        mw::hal_sensors     # IIO HAL (sysfs/eventfd; no runtime lib)
        mw::security        # full security stack (caps + sandbox + seccomp + verify)
        mw::core            # explicit: sensor_service_loop.c uses io_loop_* and
                            # memory_pool_alloc/free for sensor_reading_t pooling
        Threads::Threads
)

set_target_properties(sensor_service PROPERTIES
    OUTPUT_NAME               "sensor_service"
    POSITION_INDEPENDENT_CODE ON
    C_VISIBILITY_PRESET       hidden
)

target_compile_options(sensor_service PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-fno-omit-frame-pointer>
)

target_link_options(sensor_service PRIVATE
    "-Wl,--as-needed"
    "-Wl,-z,now"
    "-Wl,-z,relro"
    "-Wl,-z,noexecstack"
    "-Wl,--build-id=sha1"
)

install(
    TARGETS sensor_service
    RUNTIME
        DESTINATION "${CMAKE_INSTALL_SBINDIR}"
        COMPONENT   runtime
)
