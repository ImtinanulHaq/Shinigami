#!/usr/bin/env bash
# =============================================================================
# Bankai.sh — Secure Linux Middleware — Full Stack Orchestrator
# =============================================================================
#
# USAGE:
#   sudo ./Bankai.sh <command> [options]
#
# COMMANDS:
#   build      Configure and compile the project (runs cmake + make)
#   setup      Create system user, directories, and permissions
#   config     (Re)generate all service configuration files
#   run        Full stack: setup → config → start all services + TUI
#   start      Start all services in the background (no TUI)
#   stop       Gracefully stop all running services
#   status     Show what's running and what sockets are live
#   logs       Tail all service logs (Ctrl+C to exit)
#   clean      Interactive teardown: stop → sockets → build → configs → dirs → user
#   help       Show this message
#
# OPTIONS:
#   --no-tui           Run without launching the interactive TUI
#   --preset <name>    CMake preset to use (default: relwithdebinfo)
#   --install-dir <p>  Override install directory
#
# EXAMPLES:
#   sudo ./Bankai.sh build
#   sudo ./Bankai.sh run
#   sudo ./Bankai.sh run --no-tui
#   sudo ./Bankai.sh status
#   sudo ./Bankai.sh logs
#   sudo ./Bankai.sh stop
#   sudo ./Bankai.sh clean
# =============================================================================

set -uo pipefail   # Note: no -e — we handle exit codes explicitly

# =============================================================================
# 0. Colours & Logging
# =============================================================================
CYAN='\033[0;36m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
RED='\033[0;31m'; BOLD='\033[1m'; RESET='\033[0m'

log()  { echo -e "${CYAN}[$(date '+%H:%M:%S')]${RESET} $*"; }
ok()   { echo -e "${GREEN}[$(date '+%H:%M:%S')] ✓${RESET} $*"; }
warn() { echo -e "${YELLOW}[$(date '+%H:%M:%S')] ⚠${RESET}  $*"; }
fail() { echo -e "${RED}[$(date '+%H:%M:%S')] ✗${RESET} $*" >&2; }
hdr()  { echo -e "\n${BOLD}$*${RESET}"; }
die()  { fail "$*"; exit 1; }

# =============================================================================
# 1. Defaults (all overridable via env or flags)
# =============================================================================
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CMAKE_PRESET="${MW_CMAKE_PRESET:-relwithdebinfo}"
INSTALL_DIR="${MW_INSTALL_DIR:-${SCRIPT_DIR}/install/${CMAKE_PRESET}}"
BUILD_DIR="${SCRIPT_DIR}/build"
SOURCE_DIR="${SCRIPT_DIR}"

CONFIG_DIR="/etc/middleware"
LOG_DIR="/var/log/middleware"
LIB_DIR="/var/lib/middleware"
TMPFILES_CONF="/etc/tmpfiles.d/middleware.conf"

# sm_daemon binds to /run/middleware/servicemanager.sock (owned by SERVICE_USER).
# That directory is created by cmd_setup and survives via systemd-tmpfiles.
SM_SOCKET="/run/middleware/servicemanager.sock"
# Monitord socket:
MONITORD_SOCKET="/tmp/middleware_monitor.sock"
MONITORD_HTTP_PORT=9090

SERVICE_USER="servicemanager"
LAUNCH_TUI=true

declare -A PIDS
SHUTDOWN_IN_PROGRESS=false

# =============================================================================
# 2. Argument Parsing
# =============================================================================
COMMAND="${1:-help}"
shift || true

while [[ $# -gt 0 ]]; do
    case "$1" in
        --no-tui)        LAUNCH_TUI=false ;;
        --preset)        CMAKE_PRESET="$2"; INSTALL_DIR="${SCRIPT_DIR}/install/${CMAKE_PRESET}"; shift ;;
        --install-dir)   INSTALL_DIR="$2"; shift ;;
        --help|-h)       COMMAND="help" ;;
        *)               warn "Unknown option: $1" ;;
    esac
    shift
done

# =============================================================================
# 3. Prerequisite Check
# =============================================================================
check_root() {
    [[ "${EUID}" -eq 0 ]] || die "This command must be run as root (sudo ./Bankai.sh $COMMAND)"
}

check_build_tools() {
    local missing_tools=() missing_libs=()

    for tool in cmake ninja gcc g++ pkg-config openssl; do
        command -v "$tool" &>/dev/null || missing_tools+=("$tool")
    done

    # Check required dev libraries via pkg-config
    local -A lib_checks=(
        ["liburing"]="liburing-dev / liburing-devel"
        ["openssl"]="libssl-dev / openssl-devel"
        ["libcap"]="libcap-dev / libcap-devel"
        ["libseccomp"]="libseccomp-dev / libseccomp-devel"
    )
    for pkg in "${!lib_checks[@]}"; do
        pkg-config --exists "${pkg}" 2>/dev/null || missing_libs+=("${lib_checks[$pkg]}")
    done
    # ncurses not always in pkg-config — check header directly
    if ! echo '#include <ncurses.h>' | gcc -x c - -fsyntax-only 2>/dev/null; then
        missing_libs+=("libncurses-dev / ncurses-devel")
    fi

    if [[ ${#missing_tools[@]} -gt 0 ]]; then
        fail "Missing build tools: ${missing_tools[*]}"
        die "Install with: sudo apt-get install cmake ninja-build gcc g++ pkg-config openssl"
    fi
    if [[ ${#missing_libs[@]} -gt 0 ]]; then
        fail "Missing development libraries:"
        for lib in "${missing_libs[@]}"; do echo "    ✗ ${lib}"; done
        fail "Install with one of:"
        fail "  Arch:   sudo pacman -S liburing openssl libcap libseccomp ncurses"
        die  "  Debian: sudo apt-get install liburing-dev libssl-dev libcap-dev libseccomp-dev libncurses-dev"
    fi
}

is_built() {
    [[ -x "${INSTALL_DIR}/sbin/sm_daemon" ]]
}

# =============================================================================
# 4. Build
# =============================================================================
cmd_build() {
    check_root
    check_build_tools

    hdr "── Build ──────────────────────────────────────────────────────────────"

    if ! is_built; then
        log "No install found at ${INSTALL_DIR} — running full build..."
    else
        log "Rebuilding (install already exists at ${INSTALL_DIR})..."
    fi

    cd "${SOURCE_DIR}"

    # Configure
    log "Configuring with preset '${CMAKE_PRESET}'..."
    if [[ -f CMakePresets.json ]]; then
        cmake --preset "${CMAKE_PRESET}" \
              -DCMAKE_INSTALL_PREFIX="${INSTALL_DIR}" \
            || die "cmake configure failed"
    else
        # Fallback: no presets file — configure manually
        warn "CMakePresets.json not found — falling back to manual configure"
        mkdir -p "${BUILD_DIR}/${CMAKE_PRESET}"
        cmake -S . -B "${BUILD_DIR}/${CMAKE_PRESET}" \
              -DCMAKE_BUILD_TYPE=RelWithDebInfo \
              -DCMAKE_INSTALL_PREFIX="${INSTALL_DIR}" \
              -G Ninja \
            || die "cmake configure failed"
    fi

    # Build
    log "Compiling..."
    local build_target="${BUILD_DIR}/${CMAKE_PRESET}"
    cmake --build "${build_target}" --parallel "$(nproc)" \
        || die "cmake build failed"

    # Install
    log "Installing to ${INSTALL_DIR}..."
    cmake --install "${build_target}" \
        || die "cmake install failed"

    ok "Build complete → ${INSTALL_DIR}"
    ls -1 "${INSTALL_DIR}/sbin/" 2>/dev/null | sed 's/^/   /'
}

# =============================================================================
# 5. System Setup (user + directories + permissions)
# =============================================================================
cmd_setup() {
    check_root
    hdr "── System Setup ───────────────────────────────────────────────────────"

    # ── System user ──────────────────────────────────────────────────────────
    if id "${SERVICE_USER}" &>/dev/null; then
        ok "User '${SERVICE_USER}' already exists"
    else
        log "Creating system user '${SERVICE_USER}'..."
        useradd -r -s /bin/false "${SERVICE_USER}" \
            || die "Failed to create user '${SERVICE_USER}'"
        ok "User '${SERVICE_USER}' created"
    fi

    # ── Persistent directories ────────────────────────────────────────────────
    local -A dir_spec=(
        ["${CONFIG_DIR}"]="root:${SERVICE_USER}:750"
        ["${LOG_DIR}"]="${SERVICE_USER}:${SERVICE_USER}:750"
        ["${LIB_DIR}"]="${SERVICE_USER}:${SERVICE_USER}:700"
    )

    for dir in "${!dir_spec[@]}"; do
        IFS=':' read -r owner group mode <<< "${dir_spec[$dir]}"
        if [[ ! -d "$dir" ]]; then
            log "Creating ${dir}..."
            mkdir -p "$dir"
        fi
        local current_owner current_mode
        current_owner="$(stat -c '%U:%G' "$dir" 2>/dev/null || echo 'unknown:unknown')"
        current_mode="$(stat -c '%a' "$dir" 2>/dev/null || echo '000')"

        if [[ "$current_owner" != "${owner}:${group}" ]]; then
            chown "${owner}:${group}" "$dir"
            log "  ${dir}: owner → ${owner}:${group}"
        fi
        if [[ "$current_mode" != "$mode" ]]; then
            chmod "$mode" "$dir"
            log "  ${dir}: mode → ${mode}"
        fi
        ok "${dir} [${owner}:${group} ${mode}]"
    done

    # ── /run/middleware — tmpfs, needs tmpfiles.d for reboot survival ──────────
    log "Configuring /run/middleware via systemd-tmpfiles..."
    echo "d /run/middleware 0750 ${SERVICE_USER} ${SERVICE_USER} -" \
        > "${TMPFILES_CONF}"
    systemd-tmpfiles --create "${TMPFILES_CONF}" 2>/dev/null || {
        # systemd-tmpfiles not available — create manually
        mkdir -p /run/middleware
        chown "${SERVICE_USER}:${SERVICE_USER}" /run/middleware
        chmod 750 /run/middleware
    }
    ok "/run/middleware ready (tmpfiles.d configured for reboots)"

    ok "System setup complete"
}

# =============================================================================
# 6. Config Generation
# =============================================================================
cmd_config() {
    check_root
    hdr "── Configuration ──────────────────────────────────────────────────────"

    # Ensure config dir exists
    mkdir -p "${CONFIG_DIR}"

    # ── HMAC verify key (generated once, never overwritten) ──────────────────
    local KEY_FILE="${CONFIG_DIR}/middleware.key"
    if [[ ! -f "${KEY_FILE}" ]]; then
        log "Generating HMAC key..."
        openssl rand -hex 32 > "${KEY_FILE}"
        chmod 640 "${KEY_FILE}"
        chown "root:${SERVICE_USER}" "${KEY_FILE}"
        ok "HMAC key generated → ${KEY_FILE}"
    else
        ok "HMAC key already exists → ${KEY_FILE}"
    fi

    # ── Service Manager ───────────────────────────────────────────────────────
    # sm_daemon reads /etc/servicemanager.conf by default before --config flag
    # is parsed, so we write to both locations.
    for dest in "/etc/servicemanager.conf" "${CONFIG_DIR}/servicemanager.conf"; do
        cat > "${dest}" <<EOF
[socket]
path = ${SM_SOCKET}
mode = 0660

[server]
log_file = ${LOG_DIR}/servicemanager.log
max_services = 32

[security]
verify_key_file = ${KEY_FILE}
EOF
    done
    ok "servicemanager.conf"

    # ── Audio ─────────────────────────────────────────────────────────────────
    local audio_skip=0
    # Check for ALSA audio device
    local alsa_dev="hw:0,0"
    if ! aplay -l &>/dev/null 2>&1 || ! aplay -l 2>/dev/null | grep -q 'card'; then
        audio_skip=1
        log "No audio hardware found — skip_hal_init=1 for audio_service"
    fi
    cat > "${CONFIG_DIR}/audio.ini" <<EOF
[server]
socket_path = ${SM_SOCKET}
log_file = ${LOG_DIR}/audio_service.log

[servicemanager]
socket = ${SM_SOCKET}

[hardware]
device = ${alsa_dev}

[security]
verify_key_file = ${KEY_FILE}
skip_hal_init = ${audio_skip}
skip_sandbox = 1
EOF
    ok "audio.ini"

    # ── Camera ────────────────────────────────────────────────────────────────
    local camera_skip=0
    [[ ! -e /dev/video0 ]] && camera_skip=1 && log "No camera /dev/video0 — skip_hal_init=1 for camera_service"
    cat > "${CONFIG_DIR}/camera.ini" <<EOF
[server]
socket_path = ${SM_SOCKET}
log_file = ${LOG_DIR}/camera_service.log

[servicemanager]
socket = ${SM_SOCKET}

[hardware]
device = /dev/video0
width = 640
height = 480

[security]
verify_key_file = ${KEY_FILE}
skip_hal_init = ${camera_skip}
skip_sandbox = 1
EOF
    ok "camera.ini"

    # ── GPIO ──────────────────────────────────────────────────────────────────
    local gpio_skip=0
    if [[ ! -e /dev/gpiochip0 ]]; then
        gpio_skip=1
        log "No /dev/gpiochip0 — skip_hal_init=1 for gpio_service"
    else
        # Check if device has group/other access; if root-only (0600) skip HAL init
        local _gperm; _gperm=$(stat -c '%a' /dev/gpiochip0 2>/dev/null || echo "600")
        if [[ "${_gperm: -2:1}" == "0" && "${_gperm: -1:1}" == "0" ]]; then
            gpio_skip=1
            log "/dev/gpiochip0 is root-only (${_gperm}) — skip_hal_init=1 for gpio_service"
        fi
    fi
    cat > "${CONFIG_DIR}/gpio.ini" <<EOF
[server]
socket_path = ${SM_SOCKET}
log_file = ${LOG_DIR}/gpio_service.log

[servicemanager]
socket = ${SM_SOCKET}

[hardware]
chip = gpiochip0

[security]
verify_key_file = ${KEY_FILE}
skip_hal_init = ${gpio_skip}
skip_sandbox = 1
EOF
    ok "gpio.ini"

    # ── Sensor ────────────────────────────────────────────────────────────────
    # Valid sensor_type values: accel | gyro | mag  (not "accelerometer")
    local sensor_skip=0
    [[ ! -e /sys/bus/iio/devices/iio:device0 ]] && sensor_skip=1 && log "No IIO sensor device — skip_hal_init=1 for sensor_service"
    cat > "${CONFIG_DIR}/sensor.ini" <<EOF
[server]
socket_path = ${SM_SOCKET}
log_file = ${LOG_DIR}/sensor_service.log

[servicemanager]
socket = ${SM_SOCKET}

[hardware]
device_path = /sys/bus/iio/devices/iio:device0
sensor_type = accel

[security]
verify_key_file = ${KEY_FILE}
skip_hal_init = ${sensor_skip}
skip_sandbox = 1
EOF
    ok "sensor.ini"

    # ── Monitord ──────────────────────────────────────────────────────────────
    cat > "${CONFIG_DIR}/monitord.ini" <<EOF
[daemon]
unix_socket_path = ${MONITORD_SOCKET}
sm_socket_path = ${SM_SOCKET}
http_port = ${MONITORD_HTTP_PORT}
refresh_interval_ms = 1000
EOF
    ok "monitord.ini"

    # Fix ownership so servicemanager can read but not modify
    chown "root:${SERVICE_USER}" "${CONFIG_DIR}"/*
    chmod 640 "${CONFIG_DIR}"/*
    # Key stays 640 root:servicemanager — already set above
    chmod 640 "${KEY_FILE}"

    ok "All configs written to ${CONFIG_DIR}"
}

# =============================================================================
# 7. Service Lifecycle
# =============================================================================
start_process() {
    local name="$1" bin="$2"; shift 2
    if [[ ! -x "${bin}" ]]; then
        warn "Binary not found or not executable: ${bin} — skipping ${name}"
        return 1
    fi
    log "Starting ${name}..."
    "${bin}" "$@" >> "${LOG_DIR}/${name}.log" 2>&1 &
    local pid=$!
    PIDS["${name}"]="${pid}"
    # Give it 500ms for the process to either crash or daemonize
    sleep 0.5
    if kill -0 "${pid}" 2>/dev/null; then
        # Parent still alive (non-daemonizing service) — all good
        return 0
    fi
    # Parent exited — could be a clean daemonize (double-fork) or a crash.
    # Check if a child process with this name is now running.
    local daemon_pid
    daemon_pid=$(pgrep -x "${name}" 2>/dev/null | head -1 || true)
    if [[ -n "${daemon_pid}" ]]; then
        # Successfully daemonized — update tracked PID to the daemon child
        PIDS["${name}"]="${daemon_pid}"
        return 0
    fi
    fail "${name} exited immediately (check ${LOG_DIR}/${name}.log)"
    unset PIDS["${name}"]
    return 1
}

wait_for_socket() {
    local path="$1" label="${2:-socket}" timeout="${3:-15}"
    local elapsed=0
    while [[ ! -S "${path}" ]]; do
        sleep 0.5
        elapsed=$(( elapsed + 1 ))
        if (( elapsed >= timeout * 2 )); then
            fail "${label} socket never appeared at ${path}"
            return 1
        fi
    done
    ok "${label} ready (${path})"
    return 0
}

shutdown_all() {
    [[ "${SHUTDOWN_IN_PROGRESS}" == "true" ]] && return
    SHUTDOWN_IN_PROGRESS=true
    echo ""
    log "Initiating graceful shutdown..."

    # Reverse startup order: TUI → monitord → services → sm_daemon
    local ordered=("mw_tui" "monitord" "sensor_service" "gpio_service" "camera_service" "audio_service" "sm_daemon")

    for name in "${ordered[@]}"; do
        local pid="${PIDS[$name]:-}"
        [[ -z "$pid" ]] && continue
        if kill -0 "${pid}" 2>/dev/null; then
            kill -TERM "${pid}" 2>/dev/null || true
            log "  SIGTERM → ${name} (${pid})"
        fi
    done

    # Wait up to 5 seconds for clean exit, then SIGKILL stragglers
    sleep 2
    for name in "${ordered[@]}"; do
        local pid="${PIDS[$name]:-}"
        [[ -z "$pid" ]] && continue
        if kill -0 "${pid}" 2>/dev/null; then
            warn "  ${name} still alive — sending SIGKILL"
            kill -KILL "${pid}" 2>/dev/null || true
        fi
    done

    # Wait for all background jobs (suppress non-zero exits from crashes)
    for name in "${ordered[@]}"; do
        local pid="${PIDS[$name]:-}"
        [[ -z "$pid" ]] && continue
        wait "${pid}" 2>/dev/null || true
    done

    # Clean up stale sockets
    rm -f "${SM_SOCKET}" "${MONITORD_SOCKET}" /tmp/servicemanager.sock 2>/dev/null || true

    ok "All services stopped"
}

# =============================================================================
# 8. Commands: start / run / stop / status / logs
# =============================================================================
cmd_start() {
    check_root

    # Auto-build if needed
    if ! is_built; then
        warn "No build found at ${INSTALL_DIR} — running build first..."
        cmd_build
    fi

    # Auto-setup if directories/user missing
    if ! id "${SERVICE_USER}" &>/dev/null || [[ ! -d "${CONFIG_DIR}" ]]; then
        log "Running setup first..."
        cmd_setup
    fi

    # Auto-generate configs if missing
    if [[ ! -f "${CONFIG_DIR}/servicemanager.conf" ]]; then
        log "Generating configs..."
        cmd_config
    fi

    hdr "── Starting Services ──────────────────────────────────────────────────"

    # Remove any stale sockets from previous runs
    rm -f "${SM_SOCKET}" /run/servicemanager.sock "${MONITORD_SOCKET}" /tmp/servicemanager.sock 2>/dev/null || true

    # 1. Service Manager
    start_process "sm_daemon" \
        "${INSTALL_DIR}/sbin/sm_daemon" \
        --config "${CONFIG_DIR}/servicemanager.conf" \
        || die "sm_daemon failed to start"

    wait_for_socket "${SM_SOCKET}" "sm_daemon" 15 || die "sm_daemon never became ready"

    # 2. Hardware Services (best-effort — hardware may not be present)
    local hw_services=("audio" "camera" "gpio" "sensor")
    for svc in "${hw_services[@]}"; do
        start_process "${svc}_service" \
            "${INSTALL_DIR}/sbin/${svc}_service" \
            --config "${CONFIG_DIR}/${svc}.ini" || true
    done

    # 3. Monitord
    start_process "monitord" \
        "${INSTALL_DIR}/sbin/monitord" \
        --config "${CONFIG_DIR}/monitord.ini" \
        || warn "monitord failed to start (non-fatal)"

    wait_for_socket "${MONITORD_SOCKET}" "monitord" 20 \
        || warn "monitord socket slow — continuing anyway"

    ok "All services started"
    cmd_status_brief
}

cmd_run() {
    trap shutdown_all INT TERM EXIT

    cmd_start

    hdr "── Interactive TUI ────────────────────────────────────────────────────"

    if [[ "${LAUNCH_TUI}" == "true" ]]; then
        local tui_bin="${INSTALL_DIR}/sbin/mw_tui"
        if [[ -x "${tui_bin}" ]]; then
            # Ensure monitord socket is ready before launching TUI
            if [[ ! -S "${MONITORD_SOCKET}" ]]; then
                log "Waiting for monitord socket before TUI..."
                wait_for_socket "${MONITORD_SOCKET}" "monitord" 15 \
                    || warn "monitord socket unavailable — TUI will launch anyway"
            fi
            log "Launching TUI (Ctrl+C to stop everything)..."
            # Run TUI in foreground; suppress its exit code
            "${tui_bin}" || true
        else
            warn "mw_tui not found at ${tui_bin}"
            warn "Running in background mode — press Ctrl+C to stop all services"
            log "Prometheus metrics: http://localhost:${MONITORD_HTTP_PORT}/metrics"
            # Park here so trap fires on Ctrl+C
            wait "${PIDS[sm_daemon]:-}" 2>/dev/null || true
        fi
    else
        log "Running in background mode (--no-tui). Ctrl+C to stop."
        log "Prometheus metrics: http://localhost:${MONITORD_HTTP_PORT}/metrics"
        wait "${PIDS[sm_daemon]:-}" 2>/dev/null || true
    fi
}

cmd_stop() {
    check_root
    hdr "── Stop ───────────────────────────────────────────────────────────────"
    # Find and terminate any running middleware processes
    local services=("sm_daemon" "audio_service" "camera_service" "gpio_service" "sensor_service" "monitord" "mw_tui")
    local found=0
    for svc in "${services[@]}"; do
        local pids
        pids="$(pgrep -x "${svc}" 2>/dev/null || true)"
        if [[ -n "$pids" ]]; then
            echo "${pids}" | while read -r pid; do
                log "Stopping ${svc} (${pid})..."
                kill -TERM "${pid}" 2>/dev/null || true
            done
            found=1
        fi
    done
    sleep 2
    # SIGKILL anything still alive
    for svc in "${services[@]}"; do
        pkill -KILL -x "${svc}" 2>/dev/null || true
    done
    rm -f "${SM_SOCKET}" /run/servicemanager.sock "${MONITORD_SOCKET}" /tmp/servicemanager.sock 2>/dev/null || true
    if (( found )); then
        ok "Services stopped"
    else
        log "No running middleware services found"
    fi
}

cmd_status() {
    hdr "── Status ─────────────────────────────────────────────────────────────"
    local services=("sm_daemon" "audio_service" "camera_service" "gpio_service" "sensor_service" "monitord" "mw_tui")
    for svc in "${services[@]}"; do
        local pids
        pids="$(pgrep -x "${svc}" 2>/dev/null || true)"
        if [[ -n "$pids" ]]; then
            echo -e "  ${GREEN}●${RESET} ${svc}  (pid: ${pids})"
        else
            echo -e "  ${RED}○${RESET} ${svc}  (not running)"
        fi
    done
    echo ""
    echo "  Sockets:"
    for sock in "${SM_SOCKET}" /run/servicemanager.sock "${MONITORD_SOCKET}" /tmp/servicemanager.sock; do
        if [[ -S "${sock}" ]]; then
            echo -e "    ${GREEN}✓${RESET} ${sock}"
        elif [[ -e "${sock}" ]]; then
            echo -e "    ${YELLOW}?${RESET} ${sock}  (exists but not a socket)"
        fi
    done

    echo ""
    echo "  Build:   ${INSTALL_DIR}"
    if is_built; then
        echo -e "    ${GREEN}✓${RESET} sm_daemon present"
    else
        echo -e "    ${RED}✗${RESET} not built — run: sudo ./Bankai.sh build"
    fi
}

cmd_status_brief() {
    local services=("sm_daemon" "audio_service" "camera_service" "gpio_service" "sensor_service" "monitord")
    echo ""
    for svc in "${services[@]}"; do
        local pids
        pids="$(pgrep -x "${svc}" 2>/dev/null || true)"
        if [[ -n "$pids" ]]; then
            echo -e "  ${GREEN}●${RESET} ${svc}"
        else
            echo -e "  ${YELLOW}○${RESET} ${svc}  (not running)"
        fi
    done
    echo ""
}

cmd_logs() {
    hdr "── Logs (Ctrl+C to stop) ──────────────────────────────────────────────"
    local log_files=()
    for f in "${LOG_DIR}"/*.log /var/log/servicemanager.log; do
        [[ -f "$f" ]] && log_files+=("$f")
    done
    if [[ ${#log_files[@]} -eq 0 ]]; then
        warn "No log files found in ${LOG_DIR}"
        return
    fi
    tail -f "${log_files[@]}"
}

_ask() {
    # _ask "prompt text" → returns 0 (yes) or 1 (no)
    local prompt="$1"
    local resp
    read -r -p "$(echo -e "${YELLOW}${prompt} [y/N]${RESET} ")" resp
    [[ "${resp,,}" == "y" ]]
}

cmd_clean() {
    check_root
    hdr "── Clean ──────────────────────────────────────────────────────────────"

    # 0. Stop everything first — mandatory, no prompt
    log "Stopping all running services..."
    cmd_stop 2>/dev/null || true
    echo ""

    # ── Stale sockets (always cleaned — no prompt needed) ────────────────────
    rm -f "${SM_SOCKET}" /run/servicemanager.sock \
          "${MONITORD_SOCKET}" /tmp/servicemanager.sock 2>/dev/null || true
    ok "Stale sockets cleared"

    # ── CMake cache + full build tree ─────────────────────────────────────────
    # Always wipe CMakeCache — stale probe results silently break reconfigures.
    # Wipe the full build/relwithdebinfo/ tree so cmake starts completely clean.
    if [[ -d "${BUILD_DIR}" ]]; then
        find "${BUILD_DIR}" -name "CMakeCache.txt" -delete 2>/dev/null || true
        find "${BUILD_DIR}" -name "CMakeFiles" -type d -exec rm -rf {} + 2>/dev/null || true
    fi
    ok "CMake cache cleared"

    # ── Build artefacts ───────────────────────────────────────────────────────
    if _ask "Remove build artefacts? (${BUILD_DIR}/${CMAKE_PRESET} + ${INSTALL_DIR})"; then
        rm -rf "${BUILD_DIR:?}/${CMAKE_PRESET}" "${INSTALL_DIR:?}"
        ok "Build artefacts removed"
    else
        log "Build artefacts kept"
    fi

    # ── Generated config files ────────────────────────────────────────────────
    if _ask "Remove generated configs? (${CONFIG_DIR} + /etc/servicemanager.conf)"; then
        rm -rf "${CONFIG_DIR}"
        rm -f /etc/servicemanager.conf
        ok "Configs removed"
    else
        log "Configs kept"
    fi

    # ── Runtime directory (/run/middleware) ───────────────────────────────────
    if _ask "Remove runtime directory? (/run/middleware)"; then
        rm -rf /run/middleware
        ok "/run/middleware removed"
    else
        log "/run/middleware kept"
    fi

    # ── systemd tmpfiles entry ────────────────────────────────────────────────
    if _ask "Remove systemd tmpfiles config? (${TMPFILES_CONF})"; then
        rm -f "${TMPFILES_CONF}"
        ok "tmpfiles.d entry removed"
    else
        log "tmpfiles.d entry kept"
    fi

    # ── Log files ─────────────────────────────────────────────────────────────
    if _ask "Remove all log files? (${LOG_DIR} + /var/log/servicemanager.log)"; then
        rm -rf "${LOG_DIR}"
        rm -f /var/log/servicemanager.log
        ok "Logs removed"
    else
        log "Logs kept"
    fi

    # ── Persistent state (/var/lib/middleware) ────────────────────────────────
    if _ask "Remove persistent state? (${LIB_DIR} — registry.dat, HMAC seeds)"; then
        rm -rf "${LIB_DIR}"
        ok "Persistent state removed"
    else
        log "Persistent state kept"
    fi

    # ── System user (last — most destructive) ────────────────────────────────
    if id "${SERVICE_USER}" &>/dev/null; then
        if _ask "Remove system user '${SERVICE_USER}'?"; then
            userdel "${SERVICE_USER}" 2>/dev/null || true
            ok "User '${SERVICE_USER}' removed"
        else
            log "User '${SERVICE_USER}' kept"
        fi
    fi

    echo ""
    ok "Clean complete. Run 'sudo ./Bankai.sh run' to start fresh from scratch."
}

cmd_help() {
    cat <<EOF

${BOLD}Bankai.sh${RESET} — Secure Linux Middleware Orchestrator

${BOLD}USAGE${RESET}
  sudo ./Bankai.sh <command> [options]

${BOLD}COMMANDS${RESET}
  ${CYAN}build${RESET}        Configure and compile (cmake + install)
  ${CYAN}setup${RESET}        Create system user, runtime dirs, and fix permissions
  ${CYAN}config${RESET}       Generate all service config files in /etc/middleware
  ${CYAN}run${RESET}          Full stack: auto-build + auto-setup + start + TUI
  ${CYAN}start${RESET}        Start all services in background (auto-builds if needed)
  ${CYAN}stop${RESET}         Gracefully stop all running middleware services
  ${CYAN}status${RESET}       Show running services and live sockets
  ${CYAN}logs${RESET}         Tail all service log files
  ${CYAN}clean${RESET}        Interactive teardown: stop → sockets → build → configs → dirs → logs → user
  ${CYAN}help${RESET}         Show this message

${BOLD}OPTIONS${RESET}
  --no-tui              Skip the ncurses TUI (useful for CI / headless)
  --preset <name>       CMake preset (default: relwithdebinfo)
  --install-dir <path>  Override install prefix

${BOLD}FIRST RUN${RESET}
  sudo ./Bankai.sh run

${BOLD}SUBSEQUENT RUNS${RESET}
  sudo ./Bankai.sh run          # full stack
  sudo ./Bankai.sh run --no-tui # headless, Ctrl+C to stop
  sudo ./Bankai.sh status       # check what's alive

${BOLD}PROMETHEUS METRICS${RESET}
  http://localhost:${MONITORD_HTTP_PORT}/metrics

${BOLD}LOGS${RESET}
  ${LOG_DIR}/
  /var/log/servicemanager.log

EOF
}

# =============================================================================
# 9. Dispatch
# =============================================================================
case "${COMMAND}" in
    build)   cmd_build  ;;
    setup)   cmd_setup  ;;
    config)  check_root; cmd_config ;;
    run)     cmd_run    ;;
    start)   cmd_start  ;;
    stop)    cmd_stop   ;;
    status)  cmd_status ;;
    logs)    cmd_logs   ;;
    clean)   cmd_clean  ;;
    help|--help|-h)  cmd_help ;;
    *)
        fail "Unknown command: '${COMMAND}'"
        cmd_help
        exit 1
        ;;
esac
