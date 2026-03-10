# =============================================================================
# dev/services/common/CMakeLists.txt
# Shared service daemon infrastructure.
#
# Provides the three pillars every service daemon needs:
#   service_base.c    — double-fork daemonize, PID file, sigaction handlers,
#                       dual-sink logging (syslog + file), g_running/g_reload
#   service_config.c  — INI config parser, SIGHUP-triggered reload
#   service_ipc.c     — SM client: wraps sm_register / sm_heartbeat /
#                       sm_unregister with reconnection logic
#
# Produces: mw::svc_common (STATIC)
#
# Every service daemon links mw::svc_common. It transitively carries mw::sm
# (the full SM client protocol) and mw::core (io_uring, memory_pool,
# ring_buffer). Daemons do NOT need to list mw::sm or mw::core themselves
# unless they use those APIs directly beyond what service_ipc.c wraps.
# =============================================================================

add_library(mw_svc_common STATIC)
add_library(mw::svc_common ALIAS mw_svc_common)

target_sources(mw_svc_common
    PRIVATE
        service_base.c
        service_config.c
        service_ipc.c
    # FILE_SET makes headers installable and properly propagated to consumers
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
        FILES
            service_base.h
            service_config.h
            service_ipc.h
)

target_include_directories(mw_svc_common
    PUBLIC
        # Build-time: the directory these headers live in
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        # Install-time: where downstream find_package() will look
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/mw/services>
    PRIVATE
        # service_ipc.c includes SM protocol headers via relative path
        ${CMAKE_CURRENT_SOURCE_DIR}/../../core/service_manager/infrastructure
        ${CMAKE_CURRENT_SOURCE_DIR}/../../core/service_manager/lifecycle
        ${CMAKE_BINARY_DIR}/include
)

target_link_libraries(mw_svc_common
    PUBLIC
        # SM client API — sm_register/sm_lookup/sm_heartbeat/sm_unregister
        mw::sm
        # io_uring loop, memory pool, ring buffer
        mw::core
        # Hardening + sanitizer flags + platform feature macros
        mw::compile_opts
        mw::version
        # pthreads — service_base uses pthread_atfork() and sigaction handlers
        Threads::Threads
)

set_target_properties(mw_svc_common PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    C_VISIBILITY_PRESET       hidden
)

# ---------------------------------------------------------------------------
# Install — development component (headers + archive)
# ---------------------------------------------------------------------------
install(
    TARGETS mw_svc_common
    EXPORT  linux_middleware_targets
    ARCHIVE
        DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        COMPONENT   development
    FILE_SET HEADERS
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/mw/services"
        COMPONENT   development
)
