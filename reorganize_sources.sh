#!/usr/bin/env bash
# =============================================================================
# reorganize_sources.sh
# Moves all source files from the flat project root into the directory tree
# the build system expects. Run this ONCE from the project root.
#
# The source files were written with relative #include paths that assume
# this structure — e.g. audio_service_security.c includes:
#   "../../dev/security/core/security_manager.h"
# ...which only resolves correctly when the file is at:
#   dev/services/audio_service/audio_service_security.c
#
# Usage:
#   cd /path/to/Secure-Linux-Middleware
#   chmod +x reorganize_sources.sh
#   ./reorganize_sources.sh
#
# Safe to re-run — skips files that are already in the right place.
# =============================================================================

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; RESET='\033[0m'
ok()   { echo -e "${GREEN}  ✓${RESET} $*"; }
skip() { echo -e "${YELLOW}  –${RESET} $* (already there)"; }
fail() { echo -e "${RED}  ✗${RESET} $*" >&2; }

move() {
    local src="$1"
    local dst="$2"
    if [[ ! -f "$src" ]]; then
        # Already moved, or never existed — check destination
        if [[ -f "$dst" ]]; then
            skip "$dst"
        else
            fail "Source not found: $src  (target: $dst)"
        fi
        return
    fi
    if [[ "$src" -ef "$dst" ]]; then
        skip "$dst"
        return
    fi
    mkdir -p "$(dirname "$dst")"
    mv "$src" "$dst"
    ok "$src  →  $dst"
}

echo ""
echo "Reorganizing source tree for Secure-Linux-Middleware..."
echo "Root: $ROOT"
echo ""

# =============================================================================
# dev/core — io_uring event loop, memory pool, ring buffer
# =============================================================================
echo "── dev/core ──────────────────────────────────────────────────────────────"
move io_uring_loop.c     dev/core/io_uring_loop.c
move io_uring_loop.h     dev/core/io_uring_loop.h
move memory_pool.c       dev/core/memory_pool.c
move memory_pool.h       dev/core/memory_pool.h
move ring_buffer.c       dev/core/ring_buffer.c
move ring_buffer.h       dev/core/ring_buffer.h

# =============================================================================
# dev/core/service_manager/infrastructure
# =============================================================================
echo ""
echo "── dev/core/service_manager/infrastructure ───────────────────────────────"
SM_INFRA="dev/core/service_manager/infrastructure"
move sm_protocol.c        $SM_INFRA/sm_protocol.c
move sm_protocol.h        $SM_INFRA/sm_protocol.h
move sm_registry.c        $SM_INFRA/sm_registry.c
move sm_registry.h        $SM_INFRA/sm_registry.h
move sm_socket.c          $SM_INFRA/sm_socket.c
move sm_socket.h          $SM_INFRA/sm_socket.h
move sm_connection_pool.c $SM_INFRA/sm_connection_pool.c
move sm_connection_pool.h $SM_INFRA/sm_connection_pool.h
move sm_handlers.c        $SM_INFRA/sm_handlers.c
move sm_handlers.h        $SM_INFRA/sm_handlers.h
move sm_request_id.c      $SM_INFRA/sm_request_id.c
move sm_request_id.h      $SM_INFRA/sm_request_id.h

# =============================================================================
# dev/core/service_manager/security
# =============================================================================
echo ""
echo "── dev/core/service_manager/security ─────────────────────────────────────"
SM_SEC="dev/core/service_manager/security"
move sm_crypto.c             $SM_SEC/sm_crypto.c
move sm_crypto.h             $SM_SEC/sm_crypto.h
move sm_rate_limit.c         $SM_SEC/sm_rate_limit.c
move sm_rate_limit.h         $SM_SEC/sm_rate_limit.h
move sm_advanced_ratelimit.c $SM_SEC/sm_advanced_ratelimit.c
move sm_advanced_ratelimit.h $SM_SEC/sm_advanced_ratelimit.h
move sm_security.c           $SM_SEC/sm_security.c
move sm_security.h           $SM_SEC/sm_security.h

# =============================================================================
# dev/core/service_manager/lifecycle
# Note: sm_main.c contains main() — compiled into sm_daemon only, not the lib
# =============================================================================
echo ""
echo "── dev/core/service_manager/lifecycle ────────────────────────────────────"
SM_LIFE="dev/core/service_manager/lifecycle"
move sm_main.c              $SM_LIFE/sm_main.c
move sm_main.h              $SM_LIFE/sm_main.h
move sm_config.c            $SM_LIFE/sm_config.c
move sm_config.h            $SM_LIFE/sm_config.h
move sm_dependencies.c      $SM_LIFE/sm_dependencies.c
move sm_dependencies.h      $SM_LIFE/sm_dependencies.h
move sm_graceful_shutdown.c $SM_LIFE/sm_graceful_shutdown.c
move sm_graceful_shutdown.h $SM_LIFE/sm_graceful_shutdown.h
move sm_management.c        $SM_LIFE/sm_management.c
move sm_management.h        $SM_LIFE/sm_management.h
move sm_persistence.c       $SM_LIFE/sm_persistence.c
move sm_persistence.h       $SM_LIFE/sm_persistence.h
move sm_service_tier.c      $SM_LIFE/sm_service_tier.c
move sm_service_tier.h      $SM_LIFE/sm_service_tier.h

# =============================================================================
# dev/core/service_manager/observability
# =============================================================================
echo ""
echo "── dev/core/service_manager/observability ────────────────────────────────"
SM_OBS="dev/core/service_manager/observability"
move sm_health.c            $SM_OBS/sm_health.c
move sm_health.h            $SM_OBS/sm_health.h
move sm_health_callbacks.c  $SM_OBS/sm_health_callbacks.c
move sm_health_callbacks.h  $SM_OBS/sm_health_callbacks.h
move sm_metrics.c           $SM_OBS/sm_metrics.c
move sm_metrics.h           $SM_OBS/sm_metrics.h
move sm_audit.c             $SM_OBS/sm_audit.c
move sm_audit.h             $SM_OBS/sm_audit.h
move sm_logging.c           $SM_OBS/sm_logging.c
move sm_logging.h           $SM_OBS/sm_logging.h
move sm_structured_log.c    $SM_OBS/sm_structured_log.c
move sm_structured_log.h    $SM_OBS/sm_structured_log.h

# =============================================================================
# dev/core/service_manager/enterprise
# =============================================================================
echo ""
echo "── dev/core/service_manager/enterprise ───────────────────────────────────"
SM_ENT="dev/core/service_manager/enterprise"
move sm_threadpool.c       $SM_ENT/sm_threadpool.c
move sm_threadpool.h       $SM_ENT/sm_threadpool.h
move sm_tls.c              $SM_ENT/sm_tls.c
move sm_tls.h              $SM_ENT/sm_tls.h
move sm_watchdog.c         $SM_ENT/sm_watchdog.c
move sm_watchdog.h         $SM_ENT/sm_watchdog.h
move sm_rolling_restart.c  $SM_ENT/sm_rolling_restart.c
move sm_rolling_restart.h  $SM_ENT/sm_rolling_restart.h
move sm_plugin.c           $SM_ENT/sm_plugin.c
move sm_plugin.h           $SM_ENT/sm_plugin.h
move sm_eventbus.c         $SM_ENT/sm_eventbus.c
move sm_eventbus.h         $SM_ENT/sm_eventbus.h
move sm_discovery.c        $SM_ENT/sm_discovery.c
move sm_discovery.h        $SM_ENT/sm_discovery.h
move sm_container.c        $SM_ENT/sm_container.c
move sm_container.h        $SM_ENT/sm_container.h
move sm_cli.c              $SM_ENT/sm_cli.c
move sm_cli.h              $SM_ENT/sm_cli.h
move sm_main_integration.c $SM_ENT/sm_main_integration.c
move sm_main_integration.h $SM_ENT/sm_main_integration.h

# =============================================================================
# dev/hal — HAL interface + per-device layers
# =============================================================================
echo ""
echo "── dev/hal ───────────────────────────────────────────────────────────────"
move hal_interface.c  dev/hal/interface/hal_interface.c
move hal_interface.h  dev/hal/interface/hal_interface.h

move audio_hal.c  dev/hal/layers/audio/audio_hal.c
move audio_hal.h  dev/hal/layers/audio/audio_hal.h

move camera_hal.c  dev/hal/layers/camera/camera_hal.c
move camera_hal.h  dev/hal/layers/camera/camera_hal.h

move gpio_hal.c  dev/hal/layers/gpio/gpio_hal.c
move gpio_hal.h  dev/hal/layers/gpio/gpio_hal.h

move sensor_hal.c  dev/hal/layers/sensors/sensor_hal.c
move sensor_hal.h  dev/hal/layers/sensors/sensor_hal.h

# =============================================================================
# dev/security — capabilities, sandbox, seccomp, verify, core
# =============================================================================
echo ""
echo "── dev/security ──────────────────────────────────────────────────────────"
SEC_CAPS="dev/security/capabilities"
move capabilities.c        $SEC_CAPS/capabilities.c
move capabilities.h        $SEC_CAPS/capabilities.h
move capabilities_core.c   $SEC_CAPS/capabilities_core.c
move capabilities_core.h   $SEC_CAPS/capabilities_core.h
move capabilities_policy.c $SEC_CAPS/capabilities_policy.c
move capabilities_policy.h $SEC_CAPS/capabilities_policy.h
move capabilities_audit.c  $SEC_CAPS/capabilities_audit.c
move capabilities_audit.h  $SEC_CAPS/capabilities_audit.h

SEC_SB="dev/security/sandbox"
move sandbox.c          $SEC_SB/sandbox.c
move sandbox.h          $SEC_SB/sandbox.h
move sandbox_core.c     $SEC_SB/sandbox_core.c
move sandbox_core.h     $SEC_SB/sandbox_core.h
move sandbox_cgroup.c   $SEC_SB/sandbox_cgroup.c
move sandbox_cgroup.h   $SEC_SB/sandbox_cgroup.h
move sandbox_mount.c    $SEC_SB/sandbox_mount.c
move sandbox_mount.h    $SEC_SB/sandbox_mount.h
move sandbox_network.c  $SEC_SB/sandbox_network.c
move sandbox_network.h  $SEC_SB/sandbox_network.h

SEC_SC="dev/security/seccomp"
move seccomp_core.c            $SEC_SC/seccomp_core.c
move seccomp_core.h            $SEC_SC/seccomp_core.h
move seccomp_filter.c          $SEC_SC/seccomp_filter.c
move seccomp_filter.h          $SEC_SC/seccomp_filter.h
move seccomp_policy_minimal.c  $SEC_SC/seccomp_policy_minimal.c
move seccomp_policy_minimal.h  $SEC_SC/seccomp_policy_minimal.h
move seccomp_policy_audio.c    $SEC_SC/seccomp_policy_audio.c
move seccomp_policy_audio.h    $SEC_SC/seccomp_policy_audio.h
move seccomp_policy_camera.c   $SEC_SC/seccomp_policy_camera.c
move seccomp_policy_camera.h   $SEC_SC/seccomp_policy_camera.h
move seccomp_policy_sensor.c   $SEC_SC/seccomp_policy_sensor.c
move seccomp_policy_sensor.h   $SEC_SC/seccomp_policy_sensor.h
move seccomp_policy_network.c  $SEC_SC/seccomp_policy_network.c
move seccomp_policy_network.h  $SEC_SC/seccomp_policy_network.h

SEC_VER="dev/security/verify"
move verify.c         $SEC_VER/verify.c
move verify.h         $SEC_VER/verify.h
move verify_hmac.c    $SEC_VER/verify_hmac.c
move verify_hmac.h    $SEC_VER/verify_hmac.h
move verify_token.c   $SEC_VER/verify_token.c
move verify_token.h   $SEC_VER/verify_token.h
move verify_replay.c  $SEC_VER/verify_replay.c
move verify_replay.h  $SEC_VER/verify_replay.h

SEC_CORE="dev/security/core"
move security_manager.c  $SEC_CORE/security_manager.c
move security_manager.h  $SEC_CORE/security_manager.h
move sec_error.h          $SEC_CORE/sec_error.h

# =============================================================================
# dev/services/common
# =============================================================================
echo ""
echo "── dev/services/common ───────────────────────────────────────────────────"
SVC_CMN="dev/services/common"
move service_base.c    $SVC_CMN/service_base.c
move service_base.h    $SVC_CMN/service_base.h
move service_config.c  $SVC_CMN/service_config.c
move service_config.h  $SVC_CMN/service_config.h
move service_ipc.c     $SVC_CMN/service_ipc.c
move service_ipc.h     $SVC_CMN/service_ipc.h

# =============================================================================
# dev/services/audio_service
# =============================================================================
echo ""
echo "── dev/services/audio_service ────────────────────────────────────────────"
SVC_AUD="dev/services/audio_service"
move audio_service.h          $SVC_AUD/audio_service.h
move audio_service_main.c     $SVC_AUD/audio_service_main.c
move audio_service_hal.c      $SVC_AUD/audio_service_hal.c
move audio_service_hal.h      $SVC_AUD/audio_service_hal.h
move audio_service_loop.c     $SVC_AUD/audio_service_loop.c
move audio_service_loop.h     $SVC_AUD/audio_service_loop.h
move audio_service_security.c $SVC_AUD/audio_service_security.c
move audio_service_security.h $SVC_AUD/audio_service_security.h

# =============================================================================
# dev/services/camera_service
# =============================================================================
echo ""
echo "── dev/services/camera_service ───────────────────────────────────────────"
SVC_CAM="dev/services/camera_service"
move camera_service.h          $SVC_CAM/camera_service.h
move camera_service_main.c     $SVC_CAM/camera_service_main.c
move camera_service_hal.c      $SVC_CAM/camera_service_hal.c
move camera_service_hal.h      $SVC_CAM/camera_service_hal.h
move camera_service_loop.c     $SVC_CAM/camera_service_loop.c
move camera_service_loop.h     $SVC_CAM/camera_service_loop.h
move camera_service_security.c $SVC_CAM/camera_service_security.c
move camera_service_security.h $SVC_CAM/camera_service_security.h

# =============================================================================
# dev/services/gpio_service
# =============================================================================
echo ""
echo "── dev/services/gpio_service ─────────────────────────────────────────────"
SVC_GPIO="dev/services/gpio_service"
move gpio_service.h          $SVC_GPIO/gpio_service.h
move gpio_service_main.c     $SVC_GPIO/gpio_service_main.c
move gpio_service_hal.c      $SVC_GPIO/gpio_service_hal.c
move gpio_service_hal.h      $SVC_GPIO/gpio_service_hal.h
move gpio_service_loop.c     $SVC_GPIO/gpio_service_loop.c
move gpio_service_loop.h     $SVC_GPIO/gpio_service_loop.h
move gpio_service_security.c $SVC_GPIO/gpio_service_security.c
move gpio_service_security.h $SVC_GPIO/gpio_service_security.h

# =============================================================================
# dev/services/sensor_service
# =============================================================================
echo ""
echo "── dev/services/sensor_service ───────────────────────────────────────────"
SVC_SEN="dev/services/sensor_service"
move sensor_service.h          $SVC_SEN/sensor_service.h
move sensor_service_main.c     $SVC_SEN/sensor_service_main.c
move sensor_service_hal.c      $SVC_SEN/sensor_service_hal.c
move sensor_service_hal.h      $SVC_SEN/sensor_service_hal.h
move sensor_service_loop.c     $SVC_SEN/sensor_service_loop.c
move sensor_service_loop.h     $SVC_SEN/sensor_service_loop.h
move sensor_service_security.c $SVC_SEN/sensor_service_security.c
move sensor_service_security.h $SVC_SEN/sensor_service_security.h

# =============================================================================
# dev/proxy — C++ proxy library
# =============================================================================
echo ""
echo "── dev/proxy ─────────────────────────────────────────────────────────────"
PX_BASE="dev/proxy/base"
move proxy_connection.cpp    $PX_BASE/proxy_connection.cpp
move proxy_connection.h      $PX_BASE/proxy_connection.h
move proxy_event_loop.cpp    $PX_BASE/proxy_event_loop.cpp
move proxy_event_loop.h      $PX_BASE/proxy_event_loop.h
move proxy_shared_memory.cpp $PX_BASE/proxy_shared_memory.cpp
move proxy_shared_memory.h   $PX_BASE/proxy_shared_memory.h
move proxy_thread_pool.cpp   $PX_BASE/proxy_thread_pool.cpp
move proxy_thread_pool.h     $PX_BASE/proxy_thread_pool.h
move service_proxy.cpp       $PX_BASE/service_proxy.cpp
move service_proxy.h         $PX_BASE/service_proxy.h
move proxy_config.h          $PX_BASE/proxy_config.h
move proxy_result.h          $PX_BASE/proxy_result.h

move proxy_auth.cpp   dev/proxy/security/proxy_auth.cpp
move proxy_auth.h     dev/proxy/security/proxy_auth.h

move service_discovery.cpp  dev/proxy/discovery/service_discovery.cpp
move service_discovery.h    dev/proxy/discovery/service_discovery.h

PX_AUD="dev/proxy/audio"
move audio_proxy.cpp       $PX_AUD/audio_proxy.cpp
move audio_proxy.h         $PX_AUD/audio_proxy.h
move audio_shm_reader.cpp  $PX_AUD/audio_shm_reader.cpp
move audio_shm_reader.h    $PX_AUD/audio_shm_reader.h
move audio_frame.h         $PX_AUD/audio_frame.h

PX_CAM="dev/proxy/camera"
move camera_proxy.cpp       $PX_CAM/camera_proxy.cpp
move camera_proxy.h         $PX_CAM/camera_proxy.h
move camera_shm_reader.cpp  $PX_CAM/camera_shm_reader.cpp
move camera_shm_reader.h    $PX_CAM/camera_shm_reader.h
move camera_frame.h         $PX_CAM/camera_frame.h

move gpio_proxy.cpp  dev/proxy/gpio/gpio_proxy.cpp
move gpio_proxy.h    dev/proxy/gpio/gpio_proxy.h

PX_SEN="dev/proxy/sensor"
move sensor_proxy.cpp   $PX_SEN/sensor_proxy.cpp
move sensor_proxy.h     $PX_SEN/sensor_proxy.h
move sensor_reading.h   $PX_SEN/sensor_reading.h

move proxy_c_api.cpp  dev/proxy/bindings/c_api/proxy_c_api.cpp
move proxy_c_api.h    dev/proxy/bindings/c_api/proxy_c_api.h

PX_EX="dev/proxy/examples"
move audio_capture_example.cpp  $PX_EX/audio_capture_example.cpp
move camera_stream_example.cpp  $PX_EX/camera_stream_example.cpp
move sensor_poll_example.cpp    $PX_EX/sensor_poll_example.cpp

# =============================================================================
# dev/monitoring — metrics, health, alerts, tracing, collectors, daemon, tui
# =============================================================================
echo ""
echo "── dev/monitoring ────────────────────────────────────────────────────────"
MON_MET="dev/monitoring/metrics"
move metrics_registry.c          $MON_MET/metrics_registry.c
move metrics_registry.h          $MON_MET/metrics_registry.h
move metrics_history.c           $MON_MET/metrics_history.c
move metrics_history.h           $MON_MET/metrics_history.h
move metrics_delta.c             $MON_MET/metrics_delta.c
move metrics_delta.h             $MON_MET/metrics_delta.h
move metrics_export_prometheus.c $MON_MET/metrics_export_prometheus.c
move metrics_export_prometheus.h $MON_MET/metrics_export_prometheus.h
move metrics_types.h             $MON_MET/metrics_types.h

MON_HLT="dev/monitoring/health"
move health_score.c    $MON_HLT/health_score.c
move health_score.h    $MON_HLT/health_score.h
move health_history.c  $MON_HLT/health_history.c
move health_history.h  $MON_HLT/health_history.h

MON_ALT="dev/monitoring/alerts"
move alert_engine.c  $MON_ALT/alert_engine.c
move alert_engine.h  $MON_ALT/alert_engine.h
move alert_rules.c   $MON_ALT/alert_rules.c
move alert_rules.h   $MON_ALT/alert_rules.h
move alert_notify.c  $MON_ALT/alert_notify.c
move alert_notify.h  $MON_ALT/alert_notify.h

MON_TRC="dev/monitoring/tracing"
move trace_collector.c  $MON_TRC/trace_collector.c
move trace_collector.h  $MON_TRC/trace_collector.h
move trace_store.c      $MON_TRC/trace_store.c
move trace_store.h      $MON_TRC/trace_store.h
move trace_renderer.c   $MON_TRC/trace_renderer.c
move trace_renderer.h   $MON_TRC/trace_renderer.h

MON_COL="dev/monitoring/collectors"
move collector_base.c           $MON_COL/collector_base.c
move collector_base.h           $MON_COL/collector_base.h
move collector_sysinfo.c        $MON_COL/collector_sysinfo.c
move collector_sysinfo.h        $MON_COL/collector_sysinfo.h
move collector_processes.c      $MON_COL/collector_processes.c
move collector_processes.h      $MON_COL/collector_processes.h
move collector_sm.c             $MON_COL/collector_sm.c
move collector_sm.h             $MON_COL/collector_sm.h
move collector_watchdog.c       $MON_COL/collector_watchdog.c
move collector_watchdog.h       $MON_COL/collector_watchdog.h
move collector_hal.c            $MON_COL/collector_hal.c
move collector_hal.h            $MON_COL/collector_hal.h
move collector_services.c       $MON_COL/collector_services.c
move collector_services.h       $MON_COL/collector_services.h
move collector_memory_pool.c    $MON_COL/collector_memory_pool.c
move collector_memory_pool.h    $MON_COL/collector_memory_pool.h
move collector_io_uring.c       $MON_COL/collector_io_uring.c
move collector_io_uring.h       $MON_COL/collector_io_uring.h
move collector_ring_buffer.c    $MON_COL/collector_ring_buffer.c
move collector_ring_buffer.h    $MON_COL/collector_ring_buffer.h
move collector_security.c       $MON_COL/collector_security.c
move collector_security.h       $MON_COL/collector_security.h
move collector_proxy.c          $MON_COL/collector_proxy.c
move collector_proxy.h          $MON_COL/collector_proxy.h
move collector_ipc_channels.c   $MON_COL/collector_ipc_channels.c
move collector_ipc_channels.h   $MON_COL/collector_ipc_channels.h
move collector_config_watcher.c $MON_COL/collector_config_watcher.c
move collector_config_watcher.h $MON_COL/collector_config_watcher.h

MON_DM="dev/monitoring/daemon"
move monitord_main.c    $MON_DM/monitord_main.c
move monitord_config.c  $MON_DM/monitord_config.c
move monitord_config.h  $MON_DM/monitord_config.h
move monitord_state.c   $MON_DM/monitord_state.c
move monitord_state.h   $MON_DM/monitord_state.h
move monitord_server.c  $MON_DM/monitord_server.c
move monitord_server.h  $MON_DM/monitord_server.h
move monitord_http.c    $MON_DM/monitord_http.c
move monitord_http.h    $MON_DM/monitord_http.h
# middleware_monitor.c sits one level above daemon/ in monitoring root
move middleware_monitor.c  dev/monitoring/middleware_monitor.c

MON_PROTO="dev/monitoring/protocol"
move monitor_ipc_protocol.h  $MON_PROTO/monitor_ipc_protocol.h
move monitor_protocol_ver.h  $MON_PROTO/monitor_protocol_ver.h
move monitor_wire_format.h   $MON_PROTO/monitor_wire_format.h

MON_TUI="dev/monitoring/tui"
move tui_main.c       $MON_TUI/tui_main.c
move ui_engine.c      $MON_TUI/ui_engine.c
move ui_engine.h      $MON_TUI/ui_engine.h
move ui_layout.c      $MON_TUI/ui_layout.c
move ui_layout.h      $MON_TUI/ui_layout.h
move ui_input.c       $MON_TUI/ui_input.c
move ui_input.h       $MON_TUI/ui_input.h
move ui_colors.c      $MON_TUI/ui_colors.c
move ui_colors.h      $MON_TUI/ui_colors.h
move panel_overview.c $MON_TUI/panel_overview.c
move panel_overview.h $MON_TUI/panel_overview.h
move panel_topbar.c   $MON_TUI/panel_topbar.c
move panel_topbar.h   $MON_TUI/panel_topbar.h

echo ""
echo "Done. Verify with:"
echo "  find dev/ -name '*.c' -o -name '*.cpp' -o -name '*.h' | sort | head -30"
echo ""
echo "Then rebuild:"
echo "  rm -rf build/relwithdebinfo && make"
