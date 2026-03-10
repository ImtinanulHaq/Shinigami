# =============================================================================
# dev/services/common/CMakeLists.txt
# Shared service daemon infrastructure — daemonize, logging, config, IPC.
#
# Produces: mw::svc_common (STATIC)
# All four service daemons link this before their own HAL + loop code.
# =============================================================================

add_library(mw_svc_common STATIC)
add_library(mw::svc_common ALIAS mw_svc_common)

target_sources(mw_svc_common
    PRIVATE
        service_base.c
        service_config.c
        service_ipc.c
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
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/mw/services>
)

target_link_libraries(mw_svc_common
    PUBLIC
        mw::core
        mw::sm            # SM client API (sm_register, sm_heartbeat, etc.)
        mw::compile_opts
        mw::version
        Threads::Threads
)

set_target_properties(mw_svc_common PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    C_VISIBILITY_PRESET hidden
)

install(
    TARGETS mw_svc_common
    EXPORT  linux_middleware_targets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}" COMPONENT development
    FILE_SET HEADERS
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/mw/services"
    COMPONENT   development
)
