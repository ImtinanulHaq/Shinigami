cmake_minimum_required(VERSION 3.10)
project(MiddlewareMonitoring C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -O2 -fstack-protector-strong -D_POSIX_C_SOURCE=200809L")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -g")

# Find pthread
find_package(Threads REQUIRED)

# Find ncurses for TUI
find_package(Curses REQUIRED)

# ═══════════════════════════════════════════════════════════════════════════
# Protocol library (shared by daemon and TUI)
# ═══════════════════════════════════════════════════════════════════════════
add_library(monitor_protocol STATIC
    protocol/monitor_protocol_ver.h
    protocol/monitor_ipc_protocol.h
    protocol/monitor_wire_format.h
)
set_target_properties(monitor_protocol PROPERTIES LINKER_LANGUAGE C)

# ═══════════════════════════════════════════════════════════════════════════
# Metrics library
# ═══════════════════════════════════════════════════════════════════════════
add_library(monitor_metrics STATIC
    metrics/metrics_types.h
    metrics/metrics_registry.h
    metrics/metrics_registry.c
    metrics/metrics_history.h
    metrics/metrics_history.c
    metrics/metrics_delta.h
    metrics/metrics_delta.c
    metrics/metrics_export_prometheus.h
    metrics/metrics_export_prometheus.c
)
target_link_libraries(monitor_metrics Threads::Threads)

# ═══════════════════════════════════════════════════════════════════════════
# Health library
# ═══════════════════════════════════════════════════════════════════════════
add_library(monitor_health STATIC
    health/health_score.h
    health/health_score.c
    health/health_history.h
    health/health_history.c
)
target_link_libraries(monitor_health Threads::Threads)

# ═══════════════════════════════════════════════════════════════════════════
# Alerts library
# ═══════════════════════════════════════════════════════════════════════════
add_library(monitor_alerts STATIC
    alerts/alert_engine.h
    alerts/alert_engine.c
    alerts/alert_rules.h
    alerts/alert_rules.c
    alerts/alert_notify.h
    alerts/alert_notify.c
)
target_link_libraries(monitor_alerts monitor_health Threads::Threads)

# ═══════════════════════════════════════════════════════════════════════════
# Tracing library
# ═══════════════════════════════════════════════════════════════════════════
add_library(monitor_tracing STATIC
    tracing/trace_collector.h
    tracing/trace_collector.c
    tracing/trace_store.h
    tracing/trace_store.c
    tracing/trace_renderer.h
    tracing/trace_renderer.c
)
target_link_libraries(monitor_tracing Threads::Threads)

# ═══════════════════════════════════════════════════════════════════════════
# Collectors library
# ═══════════════════════════════════════════════════════════════════════════
add_library(monitor_collectors STATIC
    collectors/collector_base.h
    collectors/collector_base.c
    collectors/collector_sysinfo.h
    collectors/collector_sysinfo.c
    collectors/collector_processes.h
    collectors/collector_processes.c
    collectors/collector_sm.h
    collectors/collector_sm.c
    collectors/collector_watchdog.h
    collectors/collector_watchdog.c
    collectors/collector_hal.h
    collectors/collector_hal.c
    collectors/collector_services.h
    collectors/collector_services.c
    collectors/collector_memory_pool.h
    collectors/collector_memory_pool.c
    collectors/collector_io_uring.h
    collectors/collector_io_uring.c
    collectors/collector_ring_buffer.h
    collectors/collector_ring_buffer.c
    collectors/collector_security.h
    collectors/collector_security.c
    collectors/collector_proxy.h
    collectors/collector_proxy.c
    collectors/collector_ipc_channels.h
    collectors/collector_ipc_channels.c
    collectors/collector_config_watcher.h
    collectors/collector_config_watcher.c
)
target_link_libraries(monitor_collectors Threads::Threads)

# ═══════════════════════════════════════════════════════════════════════════
# Daemon library (common daemon code)
# ═══════════════════════════════════════════════════════════════════════════
add_library(monitor_daemon_lib STATIC
    daemon/monitord_state.h
    daemon/monitord_state.c
    daemon/monitord_config.h
    daemon/monitord_config.c
    daemon/monitord_server.h
    daemon/monitord_server.c
    daemon/monitord_http.h
    daemon/monitord_http.c
)
target_link_libraries(monitor_daemon_lib
    monitor_protocol
    monitor_metrics
    monitor_health
    monitor_alerts
    monitor_tracing
    monitor_collectors
    Threads::Threads
)

# ═══════════════════════════════════════════════════════════════════════════
# EXECUTABLE: middleware_monitord (daemon)
# ═══════════════════════════════════════════════════════════════════════════
add_executable(middleware_monitord
    daemon/monitord_main.c
)
target_link_libraries(middleware_monitord
    monitor_daemon_lib
    Threads::Threads
)

# ═══════════════════════════════════════════════════════════════════════════
# TUI library
# ═══════════════════════════════════════════════════════════════════════════
add_library(monitor_tui_lib STATIC
    tui/ui_engine.h
    tui/ui_engine.c
    tui/ui_colors.h
    tui/ui_colors.c
    tui/ui_input.h
    tui/ui_input.c
    tui/ui_layout.h
    tui/ui_layout.c
    tui/panel_topbar.h
    tui/panel_topbar.c
    tui/panel_overview.h
    tui/panel_overview.c
)
target_include_directories(monitor_tui_lib PUBLIC ${CURSES_INCLUDE_DIRS})
target_link_libraries(monitor_tui_lib
    monitor_protocol
    monitor_health
    ${CURSES_LIBRARIES}
)

# ═══════════════════════════════════════════════════════════════════════════
# EXECUTABLE: middleware_monitor_tui (TUI client)
# ═══════════════════════════════════════════════════════════════════════════
add_executable(middleware_monitor_tui
    tui/tui_main.c
)
target_link_libraries(middleware_monitor_tui
    monitor_tui_lib
    ${CURSES_LIBRARIES}
    Threads::Threads
)

# ═══════════════════════════════════════════════════════════════════════════
# EXECUTABLE: middleware_monitor (standalone, no daemon dependency)
# Reads /proc directly. Threads + ncurses only.
# ═══════════════════════════════════════════════════════════════════════════
add_executable(middleware_monitor
    middleware_monitor.c
)
target_compile_options(middleware_monitor PRIVATE -D_GNU_SOURCE -Wno-unused-result)
target_include_directories(middleware_monitor PRIVATE ${CURSES_INCLUDE_DIRS})
target_link_libraries(middleware_monitor
    ${CURSES_LIBRARIES}
    Threads::Threads
)

# ═══════════════════════════════════════════════════════════════════════════
# Install targets
# ═══════════════════════════════════════════════════════════════════════════
install(TARGETS middleware_monitord middleware_monitor_tui middleware_monitor
    RUNTIME DESTINATION bin
)
