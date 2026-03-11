# =============================================================================
# dev/monitoring/CMakeLists.txt
# Monitoring stack: monitord daemon + interactive TUI + alert engine.
#
# Produces:
#   mw_mon_collectors  — STATIC: all 14 collector implementations
#   mw_mon_metrics     — STATIC: metrics registry, history, delta, prometheus
#   mw_mon_health      — STATIC: health scoring and history
#   mw_mon_alerts      — STATIC: alert engine, rules, notify
#   mw_mon_tracing     — STATIC: distributed trace collector + renderer
#   monitord           — EXECUTABLE: monitoring daemon
#   mw_tui             — EXECUTABLE: terminal UI (optional, requires ncurses)
# =============================================================================

if(NOT MW_BUILD_MONITORING)
    message(STATUS "[MW] Monitoring: skipped (MW_BUILD_MONITORING=OFF)")
    return()
endif()

# Shared include root for intra-monitoring includes
set(MON_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")

# ---------------------------------------------------------------------------
# Helper macro
# ---------------------------------------------------------------------------
macro(mw_mon_sublibrary _target _src_dir)
    cmake_parse_arguments("_ML" "" "" "SOURCES;LINK_LIBS" ${ARGN})

    add_library(${_target} STATIC)

    foreach(_src ${_ML_SOURCES})
        target_sources(${_target} PRIVATE "${_src_dir}/${_src}")
    endforeach()

    target_include_directories(${_target}
        PUBLIC
            $<BUILD_INTERFACE:${MON_ROOT}/${_src_dir}>
            $<BUILD_INTERFACE:${MON_ROOT}>
            $<BUILD_INTERFACE:${MON_ROOT}/protocol>
            $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/include>
    )

    target_link_libraries(${_target}
        PUBLIC mw::compile_opts ${_ML_LINK_LIBS}
    )

    set_target_properties(${_target} PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        C_VISIBILITY_PRESET hidden
    )
endmacro()

# ---------------------------------------------------------------------------
# 1. Metrics — registry, history, delta computation, Prometheus exporter
# ---------------------------------------------------------------------------
mw_mon_sublibrary(mw_mon_metrics "metrics"
    SOURCES
        metrics_registry.c
        metrics_history.c
        metrics_delta.c
        metrics_export_prometheus.c
    LINK_LIBS
        mw::core
        Threads::Threads
)

# ---------------------------------------------------------------------------
# 2. Health — scoring, history (1-hour @ 1s resolution)
# ---------------------------------------------------------------------------
mw_mon_sublibrary(mw_mon_health "health"
    SOURCES
        health_score.c
        health_history.c
    LINK_LIBS
        mw_mon_metrics
        mw::core
)

# ---------------------------------------------------------------------------
# 3. Alerts — rule engine, deduplification, notification dispatch
# ---------------------------------------------------------------------------
mw_mon_sublibrary(mw_mon_alerts "alerts"
    SOURCES
        alert_engine.c
        alert_rules.c
        alert_notify.c
    LINK_LIBS
        mw_mon_metrics
        mw_mon_health
        mw::core
)

# ---------------------------------------------------------------------------
# 4. Distributed tracing — collector, store, renderer
# ---------------------------------------------------------------------------
mw_mon_sublibrary(mw_mon_tracing "tracing"
    SOURCES
        trace_collector.c
        trace_store.c
        trace_renderer.c
    LINK_LIBS
        mw::core
)

# ---------------------------------------------------------------------------
# 5. Collectors — all 14, each pulling data from a specific platform component
# ---------------------------------------------------------------------------
mw_mon_sublibrary(mw_mon_collectors "collectors"
    SOURCES
        collector_base.c
        collector_sysinfo.c
        collector_processes.c
        collector_sm.c
        collector_watchdog.c
        collector_hal.c
        collector_services.c
        collector_memory_pool.c
        collector_io_uring.c
        collector_ring_buffer.c
        collector_security.c
        collector_proxy.c
        collector_ipc_channels.c
        collector_config_watcher.c
    LINK_LIBS
        mw_mon_metrics
        mw_mon_health
        mw::core
        mw::hal_iface
        Threads::Threads
)

# ---------------------------------------------------------------------------
# 6. monitord daemon — the monitoring server
# ---------------------------------------------------------------------------
add_executable(monitord)

target_sources(monitord
    PRIVATE
        daemon/monitord_main.c
        daemon/monitord_config.c
        daemon/monitord_state.c
        daemon/monitord_server.c
        daemon/monitord_http.c
)

target_include_directories(monitord
    PRIVATE
        ${MON_ROOT}
        ${MON_ROOT}/daemon
        ${MON_ROOT}/protocol
        ${CMAKE_BINARY_DIR}/include
)

target_link_libraries(monitord
    PRIVATE
        mw_mon_collectors
        mw_mon_metrics
        mw_mon_health
        mw_mon_alerts
        mw_mon_tracing
        mw::core
        mw::sm            # connects to SM for service state
        Threads::Threads
)

set_target_properties(monitord PROPERTIES
    OUTPUT_NAME "monitord"
    POSITION_INDEPENDENT_CODE ON
)

target_link_options(monitord PRIVATE
    "-Wl,--as-needed"
    "-Wl,-z,now"
    "-Wl,-z,relro"
    "-Wl,-z,noexecstack"
)

# ---------------------------------------------------------------------------
# 7. TUI — interactive terminal dashboard (ncurses)
# ---------------------------------------------------------------------------
add_executable(mw_tui_bin)
set_target_properties(mw_tui_bin PROPERTIES OUTPUT_NAME "mw_tui")

target_sources(mw_tui_bin
    PRIVATE
        tui/tui_main.c
        tui/ui_engine.c
        tui/ui_layout.c
        tui/ui_input.c
        tui/ui_colors.c
        tui/panel_overview.c
        tui/panel_topbar.c
        tui/panel_services.c
        tui/panel_hal.c
        tui/panel_memory.c
        tui/panel_io.c
        tui/panel_security.c
        tui/panel_alerts.c
        tui/panel_logs.c
        tui/panel_traces.c
        tui/panel_help.c
)

target_link_libraries(mw_tui_bin
    PRIVATE
        ${CURSES_LIBRARIES}
        mw::compile_opts
        mw::core
        Threads::Threads
        mw_mon_health
)

# All TUI include dirs in one call: sources, protocol, generated headers, ncurses
target_include_directories(mw_tui_bin
    PRIVATE
        ${MON_ROOT}/tui
        ${MON_ROOT}/protocol
        ${CMAKE_BINARY_DIR}/include
        ${CURSES_INCLUDE_DIRS}
)

target_link_options(mw_tui_bin PRIVATE
    "-Wl,--as-needed"
    "-Wl,-z,now"
    "-Wl,-z,relro"
    "-Wl,-z,noexecstack"
)

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
install(
    TARGETS monitord mw_tui_bin
    RUNTIME DESTINATION "${CMAKE_INSTALL_SBINDIR}" COMPONENT monitoring
)

install(
    TARGETS mw_mon_collectors mw_mon_metrics mw_mon_health
            mw_mon_alerts mw_mon_tracing
    EXPORT  linux_middleware_targets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}" COMPONENT development
)

install(
    FILES
        protocol/monitor_ipc_protocol.h
        protocol/monitor_protocol_ver.h
        protocol/monitor_wire_format.h
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/mw/monitoring"
    COMPONENT   development
)
