#!/usr/bin/env bash
# =============================================================================
# main_monitor.sh — linux-middleware complete system launcher
#
# Starts the full platform in dependency order, monitors all processes,
# and shuts everything down cleanly on SIGTERM/SIGINT.
#
# Dependency order (strict — do not reorder):
#   1. sm_daemon          — must be up before anything registers
#   2. audio_service      \
#      camera_service      |— register with SM; can start in parallel
#      gpio_service        |
#      sensor_service     /
#   3. monitord           — connects to SM to read service state
#   4. mw_tui             — connects to monitord (optional, foreground only)
#
# Usage:
#   sudo ./main_monitor.sh                  # start everything
#   sudo ./main_monitor.sh --no-tui         # headless (no terminal UI)
#   sudo ./main_monitor.sh --config-dir /path/to/configs
#   sudo ./main_monitor.sh --install-dir /usr/local
#   sudo ./main_monitor.sh --status         # check if system is running
#   sudo ./main_monitor.sh --stop           # stop a running system
#
# Exit codes:
#   0  — clean shutdown
#   1  — startup failure (binary not found, socket never appeared, etc.)
#   2  — runtime failure (a service died and could not be restarted)
# =============================================================================

set -euo pipefail

# ---------------------------------------------------------------------------
# Defaults — override with CLI flags or environment variables
# ---------------------------------------------------------------------------
INSTALL_DIR="${MW_INSTALL_DIR:-/usr/local}"
CONFIG_DIR="${MW_CONFIG_DIR:-/etc/middleware}"
LOG_DIR="${MW_LOG_DIR:-/var/log/middleware}"
RUN_DIR="${MW_RUN_DIR:-/run/middleware}"
LAUNCH_TUI="${MW_TUI:-true}"

# Socket paths (must match source defaults in sm_socket.h / monitord_config.h)
SM_SOCKET="${MW_SM_SOCKET:-/run/servicemanager.sock}"
MONITORD_SOCKET="${MW_MONITORD_SOCKET:-/tmp/middleware_monitor.sock}"
MONITORD_HTTP_PORT="${MW_HTTP_PORT:-9090}"

# Startup timeouts (seconds)
SM_READY_TIMEOUT=10
SERVICE_READY_TIMEOUT=15
MONITORD_READY_TIMEOUT=10

# Health check interval (seconds)
WATCHDOG_INTERVAL=5

# Max automatic restarts per service before giving up
MAX_RESTARTS=3

# ---------------------------------------------------------------------------
# Internal state
# ---------------------------------------------------------------------------
declare -A PIDS          # process name → PID
declare -A RESTART_COUNT # process name → restart count
SHUTDOWN_IN_PROGRESS=false
SCRIPT_PID=$$

# ---------------------------------------------------------------------------
# Colour output — auto-disabled if not a terminal
# ---------------------------------------------------------------------------
if [[ -t 1 ]]; then
    RED='\033[0;31m'; YELLOW='\033[1;33m'; GREEN='\033[0;32m'
    CYAN='\033[0;36m'; BOLD='\033[1m'; RESET='\033[0m'
else
    RED=''; YELLOW=''; GREEN=''; CYAN=''; BOLD=''; RESET=''
fi

log()  { echo -e "${CYAN}[$(date '+%H:%M:%S')]${RESET} $*"; }
ok()   { echo -e "${GREEN}[$(date '+%H:%M:%S')] ✓${RESET} $*"; }
warn() { echo -e "${YELLOW}[$(date '+%H:%M:%S')] ⚠${RESET} $*"; }
fail() { echo -e "${RED}[$(date '+%H:%M:%S')] ✗${RESET} $*" >&2; }
die()  { fail "$*"; exit 1; }

# ---------------------------------------------------------------------------
# CLI parsing
# ---------------------------------------------------------------------------
parse_args() {
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --no-tui)         LAUNCH_TUI=false ;;
            --config-dir)     CONFIG_DIR="$2";   shift ;;
            --install-dir)    INSTALL_DIR="$2";  shift ;;
            --log-dir)        LOG_DIR="$2";      shift ;;
            --status)         cmd_status; exit 0 ;;
            --stop)           cmd_stop;   exit 0 ;;
            --help|-h)        usage;      exit 0 ;;
            *) die "Unknown argument: $1. Run with --help for usage." ;;
        esac
        shift
    done
}

usage() {
    cat <<EOF
Usage: $0 [OPTIONS]

Options:
  --no-tui              Start without the terminal UI (headless mode)
  --config-dir PATH     Config file directory (default: /etc/middleware)
  --install-dir PATH    Installation prefix   (default: /usr/local)
  --log-dir PATH        Log file directory    (default: /var/log/middleware)
  --status              Check system status and exit
  --stop                Stop a running system and exit
  --help                Show this message

Environment overrides (same as CLI flags):
  MW_INSTALL_DIR, MW_CONFIG_DIR, MW_LOG_DIR, MW_TUI (true/false)
  MW_SM_SOCKET, MW_MONITORD_SOCKET, MW_HTTP_PORT
EOF
}

# ---------------------------------------------------------------------------
# Binary resolution — absolute paths, verified to exist before starting
# ---------------------------------------------------------------------------
resolve_bins() {
    SBIN="${INSTALL_DIR}/sbin"
    BIN="${INSTALL_DIR}/bin"

    SM_BIN="${SBIN}/sm_daemon"
    AUDIO_BIN="${SBIN}/audio_service"
    CAMERA_BIN="${SBIN}/camera_service"
    GPIO_BIN="${SBIN}/gpio_service"
    SENSOR_BIN="${SBIN}/sensor_service"
    MONITORD_BIN="${SBIN}/monitord"
    TUI_BIN="${BIN}/mw_tui"

    # SM daemon is mandatory
    [[ -x "${SM_BIN}" ]] || die "sm_daemon not found at ${SM_BIN}. Did you run 'make install'?"

    # Service daemons — warn if missing, don't fail
    # (a camera-less deployment is legitimate)
    for _pair in "audio:${AUDIO_BIN}" "camera:${CAMERA_BIN}" \
                 "gpio:${GPIO_BIN}" "sensor:${SENSOR_BIN}"; do
        local _name="${_pair%%:*}"
        local _path="${_pair##*:}"
        if [[ ! -x "${_path}" ]]; then
            warn "${_name}_service not found at ${_path} — skipping"
            eval "${_name^^}_ENABLED=false"
        else
            eval "${_name^^}_ENABLED=true"
        fi
    done

    [[ -x "${MONITORD_BIN}" ]] || \
        warn "monitord not found at ${MONITORD_BIN} — monitoring disabled"

    [[ -x "${TUI_BIN}" ]] || LAUNCH_TUI=false
}

# ---------------------------------------------------------------------------
# Config file generation — creates minimal INI files if not present.
# Production deployments should manage these via their own config management.
# ---------------------------------------------------------------------------
ensure_configs() {
    mkdir -p "${CONFIG_DIR}" "${LOG_DIR}" "${RUN_DIR}"

    # SM config
    if [[ ! -f "${CONFIG_DIR}/servicemanager.conf" ]]; then
        log "Generating minimal SM config at ${CONFIG_DIR}/servicemanager.conf"
        cat > "${CONFIG_DIR}/servicemanager.conf" <<CONF
[socket]
path = ${SM_SOCKET}
mode = 0660

[server]
log_file = ${LOG_DIR}/servicemanager.log
max_services = 32
heartbeat_timeout = 10

[persistence]
file = /var/lib/middleware/registry.dat
CONF
    fi

    # Per-service configs
    for _svc in audio camera gpio sensor; do
        if [[ ! -f "${CONFIG_DIR}/${_svc}.ini" ]]; then
            log "Generating minimal config for ${_svc}_service"
            cat > "${CONFIG_DIR}/${_svc}.ini" <<CONF
[server]
log_file = ${LOG_DIR}/${_svc}_service.log
foreground = false
verbose = false

[servicemanager]
socket = ${SM_SOCKET}

[security]
key_file = /etc/middleware/middleware.key
CONF
        fi
    done

    # monitord config
    if [[ ! -f "${CONFIG_DIR}/monitord.ini" ]]; then
        log "Generating minimal monitord config"
        cat > "${CONFIG_DIR}/monitord.ini" <<CONF
[daemon]
unix_socket_path = ${MONITORD_SOCKET}
http_port = ${MONITORD_HTTP_PORT}
refresh_interval_ms = 1000
CONF
    fi

    # Ensure state directory exists (for SM persistence)
    mkdir -p /var/lib/middleware
}

# ---------------------------------------------------------------------------
# Signal handling — clean shutdown on SIGTERM/SIGINT/SIGQUIT
# ---------------------------------------------------------------------------
setup_signals() {
    trap 'handle_signal SIGTERM' TERM
    trap 'handle_signal SIGINT'  INT
    trap 'handle_signal SIGQUIT' QUIT
    # SIGHUP — reload configs (forward to all running services)
    trap 'handle_reload' HUP
}

handle_signal() {
    local _sig="$1"
    if [[ "${SHUTDOWN_IN_PROGRESS}" == "true" ]]; then return; fi
    SHUTDOWN_IN_PROGRESS=true
    log "Received ${_sig} — initiating clean shutdown..."
    shutdown_all
}

handle_reload() {
    log "SIGHUP received — forwarding config reload to all services"
    for _name in "${!PIDS[@]}"; do
        local _pid="${PIDS[$_name]}"
        if kill -0 "${_pid}" 2>/dev/null; then
            kill -HUP "${_pid}" 2>/dev/null || true
            log "SIGHUP forwarded to ${_name} (PID ${_pid})"
        fi
    done
}

# ---------------------------------------------------------------------------
# Process launch helpers
# ---------------------------------------------------------------------------

# start_process <name> <binary> [args...]
# Starts the binary, records its PID, sets restart count to 0.
start_process() {
    local _name="$1"; shift
    local _bin="$1";  shift

    RESTART_COUNT["${_name}"]="${RESTART_COUNT[${_name}]:-0}"

    log "Starting ${_name} (${_bin} $*)"
    "${_bin}" "$@" &
    PIDS["${_name}"]=$!
    ok "${_name} started (PID ${PIDS[${_name}]})"
}

# wait_for_socket <path> <timeout_s> <service_name>
# Polls until a Unix socket appears or timeout expires.
wait_for_socket() {
    local _socket="$1"
    local _timeout="$2"
    local _name="$3"
    local _elapsed=0

    log "Waiting for ${_name} socket at ${_socket}..."
    while [[ ! -S "${_socket}" ]]; do
        sleep 0.2
        _elapsed=$(( _elapsed + 1 ))
        if (( _elapsed > _timeout * 5 )); then
            die "${_name} socket did not appear within ${_timeout}s — aborting."
        fi
    done
    ok "${_name} socket ready (${_socket})"
}

# wait_for_http <port> <timeout_s> <service_name>
wait_for_http() {
    local _port="$1"
    local _timeout="$2"
    local _name="$3"
    local _elapsed=0

    log "Waiting for ${_name} HTTP on port ${_port}..."
    while ! (echo > /dev/tcp/127.0.0.1/"${_port}") 2>/dev/null; do
        sleep 0.2
        _elapsed=$(( _elapsed + 1 ))
        if (( _elapsed > _timeout * 5 )); then
            warn "${_name} HTTP port ${_port} not ready after ${_timeout}s — continuing anyway"
            return
        fi
    done
    ok "${_name} HTTP ready on port ${_port}"
}

# ---------------------------------------------------------------------------
# Startup sequence
# ---------------------------------------------------------------------------
start_system() {
    echo ""
    echo -e "${BOLD}linux-middleware — starting platform${RESET}"
    echo "  Install dir : ${INSTALL_DIR}"
    echo "  Config dir  : ${CONFIG_DIR}"
    echo "  Log dir     : ${LOG_DIR}"
    echo "  SM socket   : ${SM_SOCKET}"
    echo ""

    # Step 1 — Service Manager (must be first)
    start_process "sm_daemon" "${SM_BIN}" \
        --config "${CONFIG_DIR}/servicemanager.conf"
    wait_for_socket "${SM_SOCKET}" "${SM_READY_TIMEOUT}" "sm_daemon"

    # Step 2 — Hardware service daemons (parallel start, staggered 200ms)
    # Each independently registers with SM, so order within this group doesn't matter.
    local _stagger=0
    for _svc in audio camera gpio sensor; do
        local _enabled_var="${_svc^^}_ENABLED"
        local _bin_var="${_svc^^}_BIN"
        if [[ "${!_enabled_var}" == "true" ]]; then
            [[ "${_stagger}" -gt 0 ]] && sleep 0.2
            start_process "${_svc}_service" "${!_bin_var}" \
                --config "${CONFIG_DIR}/${_svc}.ini"
            _stagger=$(( _stagger + 1 ))
        fi
    done

    # Brief pause — let services complete SM registration before monitord queries
    sleep 1

    # Step 3 — Monitoring daemon
    if [[ -x "${MONITORD_BIN}" ]]; then
        start_process "monitord" "${MONITORD_BIN}" \
            --config "${CONFIG_DIR}/monitord.ini"
        wait_for_socket "${MONITORD_SOCKET}" "${MONITORD_READY_TIMEOUT}" "monitord"
        wait_for_http "${MONITORD_HTTP_PORT}" "${MONITORD_READY_TIMEOUT}" "monitord"
    fi

    echo ""
    ok "Platform is up. Running processes:"
    for _name in "${!PIDS[@]}"; do
        printf "  %-20s PID %s\n" "${_name}" "${PIDS[$_name]}"
    done
    echo ""
    log "Metrics available at: http://localhost:${MONITORD_HTTP_PORT}/metrics"
    echo ""
}

# ---------------------------------------------------------------------------
# Watchdog — health checks and automatic restart
# ---------------------------------------------------------------------------
run_watchdog() {
    log "Watchdog started (interval: ${WATCHDOG_INTERVAL}s, max restarts: ${MAX_RESTARTS})"

    while [[ "${SHUTDOWN_IN_PROGRESS}" == "false" ]]; do
        sleep "${WATCHDOG_INTERVAL}"
        [[ "${SHUTDOWN_IN_PROGRESS}" == "true" ]] && break

        for _name in "${!PIDS[@]}"; do
            local _pid="${PIDS[$_name]}"

            # Check if the process is still alive
            if ! kill -0 "${_pid}" 2>/dev/null; then
                local _restarts="${RESTART_COUNT[$_name]:-0}"

                if (( _restarts >= MAX_RESTARTS )); then
                    fail "${_name} (PID ${_pid}) is DEAD and exceeded max restarts (${MAX_RESTARTS})"
                    fail "System integrity compromised — initiating emergency shutdown"
                    SHUTDOWN_IN_PROGRESS=true
                    shutdown_all
                    exit 2
                fi

                warn "${_name} (PID ${_pid}) died — restarting (attempt $(( _restarts + 1 ))/${MAX_RESTARTS})"
                RESTART_COUNT["${_name}"]=$(( _restarts + 1 ))

                # SM daemon death requires full system restart — everything depends on it
                if [[ "${_name}" == "sm_daemon" ]]; then
                    fail "sm_daemon died — this is fatal. All services will lose SM registration."
                    fail "Full system restart required."
                    SHUTDOWN_IN_PROGRESS=true
                    shutdown_all
                    exit 2
                fi

                # Restart the specific service
                restart_process "${_name}"
            fi
        done
    done
}

restart_process() {
    local _name="$1"
    case "${_name}" in
        audio_service)
            start_process "${_name}" "${AUDIO_BIN}" \
                --config "${CONFIG_DIR}/audio.ini" ;;
        camera_service)
            start_process "${_name}" "${CAMERA_BIN}" \
                --config "${CONFIG_DIR}/camera.ini" ;;
        gpio_service)
            start_process "${_name}" "${GPIO_BIN}" \
                --config "${CONFIG_DIR}/gpio.ini" ;;
        sensor_service)
            start_process "${_name}" "${SENSOR_BIN}" \
                --config "${CONFIG_DIR}/sensor.ini" ;;
        monitord)
            start_process "${_name}" "${MONITORD_BIN}" \
                --config "${CONFIG_DIR}/monitord.ini" ;;
        *)
            warn "Don't know how to restart '${_name}' — skipping" ;;
    esac
}

# ---------------------------------------------------------------------------
# Shutdown sequence (reverse dependency order)
# ---------------------------------------------------------------------------
shutdown_all() {
    log "Shutting down platform..."

    # Step 1 — TUI first (it's just a display, has no dependents)
    _stop_process "mw_tui"     SIGTERM 3

    # Step 2 — monitord (depends on SM, so stop before SM)
    _stop_process "monitord"   SIGTERM 5

    # Step 3 — service daemons in parallel (they'll cleanly unregister from SM)
    for _svc in sensor gpio camera audio; do
        _stop_process "${_svc}_service" SIGTERM 5 &
    done
    wait

    # Step 4 — SM daemon last (give it time to write persistence file)
    _stop_process "sm_daemon"  SIGTERM 8

    ok "Platform stopped cleanly."
}

# _stop_process <name> <signal> <timeout_s>
_stop_process() {
    local _name="$1"
    local _sig="$2"
    local _timeout="$3"
    local _pid="${PIDS[$_name]:-}"

    [[ -z "${_pid}" ]] && return
    if ! kill -0 "${_pid}" 2>/dev/null; then return; fi

    log "Stopping ${_name} (PID ${_pid}) with ${_sig}..."
    kill "-${_sig}" "${_pid}" 2>/dev/null || true

    local _elapsed=0
    while kill -0 "${_pid}" 2>/dev/null; do
        sleep 0.5
        _elapsed=$(( _elapsed + 1 ))
        if (( _elapsed > _timeout * 2 )); then
            warn "${_name} did not stop within ${_timeout}s — sending SIGKILL"
            kill -KILL "${_pid}" 2>/dev/null || true
            break
        fi
    done

    ok "${_name} stopped"
    unset "PIDS[${_name}]"
}

# ---------------------------------------------------------------------------
# --status command
# ---------------------------------------------------------------------------
cmd_status() {
    local _pidfile="${RUN_DIR}/launcher.pid"
    if [[ ! -f "${_pidfile}" ]]; then
        echo "System is not running (no PID file at ${_pidfile})"
        return 1
    fi
    local _pid
    _pid=$(<"${_pidfile}")
    if kill -0 "${_pid}" 2>/dev/null; then
        echo "System is running (launcher PID ${_pid})"
        echo ""
        echo "Socket status:"
        [[ -S "${SM_SOCKET}" ]]        && echo "  SM:       ${SM_SOCKET} ✓" \
                                       || echo "  SM:       ${SM_SOCKET} ✗ (not found)"
        [[ -S "${MONITORD_SOCKET}" ]]  && echo "  monitord: ${MONITORD_SOCKET} ✓" \
                                       || echo "  monitord: ${MONITORD_SOCKET} ✗ (not found)"
    else
        echo "System is NOT running (stale PID file for PID ${_pid})"
        return 1
    fi
}

# ---------------------------------------------------------------------------
# --stop command
# ---------------------------------------------------------------------------
cmd_stop() {
    local _pidfile="${RUN_DIR}/launcher.pid"
    if [[ ! -f "${_pidfile}" ]]; then
        echo "System does not appear to be running (no PID file)"
        return 0
    fi
    local _pid
    _pid=$(<"${_pidfile}")
    if kill -0 "${_pid}" 2>/dev/null; then
        log "Sending SIGTERM to launcher (PID ${_pid})"
        kill -TERM "${_pid}"
        log "Waiting for clean shutdown..."
        local _n=0
        while kill -0 "${_pid}" 2>/dev/null; do
            sleep 0.5
            _n=$(( _n + 1 ))
            if (( _n > 30 )); then
                warn "Launcher did not exit — sending SIGKILL"
                kill -KILL "${_pid}" 2>/dev/null || true
                break
            fi
        done
        ok "System stopped."
    else
        warn "Stale PID file (PID ${_pid} not running). Removing."
        rm -f "${_pidfile}"
    fi
}

# ---------------------------------------------------------------------------
# Write PID file so --status/--stop can find us
# ---------------------------------------------------------------------------
write_pidfile() {
    mkdir -p "${RUN_DIR}"
    echo "${SCRIPT_PID}" > "${RUN_DIR}/launcher.pid"
}

remove_pidfile() {
    rm -f "${RUN_DIR}/launcher.pid"
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
main() {
    parse_args "$@"
    setup_signals

    # Must run as root (seccomp, capabilities, namespace setup require it)
    if [[ "${EUID}" -ne 0 ]]; then
        die "This script must run as root (the daemons drop privileges internally)."
    fi

    resolve_bins
    ensure_configs
    write_pidfile
    trap remove_pidfile EXIT

    start_system

    # Step 4 — TUI (optional, foreground; blocks here if enabled)
    if [[ "${LAUNCH_TUI}" == "true" ]] && [[ -x "${TUI_BIN}" ]]; then
        log "Launching TUI (Ctrl-C or 'q' to exit)..."
        # TUI runs in foreground; watchdog runs in background
        run_watchdog &
        WATCHDOG_PID=$!
        "${TUI_BIN}" || true
        # TUI exited — clean shutdown
        kill "${WATCHDOG_PID}" 2>/dev/null || true
        SHUTDOWN_IN_PROGRESS=true
        shutdown_all
    else
        # Headless — watchdog is the main loop
        log "Running headless (--no-tui). Send SIGTERM or run '$0 --stop' to stop."
        run_watchdog
    fi

    remove_pidfile
    exit 0
}

main "$@"
