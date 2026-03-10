#!/usr/bin/env bash
# =============================================================================
# Bankai.sh — Secure Linux Middleware Orchestrator
# =============================================================================

set -euo pipefail

# ---------------------------------------------------------------------------
# 1. Configuration & Paths
# ---------------------------------------------------------------------------
INSTALL_DIR="${MW_INSTALL_DIR:-$(pwd)/install/relwithdebinfo}"
CONFIG_DIR="/etc/middleware"
LOG_DIR="/var/log/middleware"
LIB_DIR="/var/lib/middleware"

RUN_DIR="/tmp/middleware"
SM_SOCKET="/run/middleware/servicemanager.sock"

# Standardizing on /run/middleware to avoid root-level permission issues
MONITORD_SOCKET="/tmp/middleware_monitor.sock"
MONITORD_HTTP_PORT=9090

# Execution Targets
SERVICE_USER="servicemanager"
LAUNCH_TUI=true

# ---------------------------------------------------------------------------
# 2. Internal State & UI
# ---------------------------------------------------------------------------
declare -A PIDS
declare -A RESTART_COUNT
MAX_RESTARTS=3
WATCHDOG_INTERVAL=5
SHUTDOWN_IN_PROGRESS=false

CYAN='\033[0;36m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; RED='\033[0;31m'; RESET='\033[0m'

log()  { echo -e "${CYAN}[$(date '+%H:%M:%S')]${RESET} $*"; }
ok()   { echo -e "${GREEN}[$(date '+%H:%M:%S')] ✓${RESET} $*"; }
warn() { echo -e "${YELLOW}[$(date '+%H:%M:%S')] ⚠${RESET} $*"; }
fail() { echo -e "${RED}[$(date '+%H:%M:%S')] ✗${RESET} $*" >&2; }

# ---------------------------------------------------------------------------
# 3. Environment Setup (The "Clean Room" Logic)
# ---------------------------------------------------------------------------
prepare_environment() {
    log "Preparing system environment..."
    if ! id "$SERVICE_USER" &>/dev/null; then
        useradd -r -s /bin/false "$SERVICE_USER" || true
    fi

    # Create everything
    mkdir -p "$CONFIG_DIR" "$LOG_DIR" "$LIB_DIR"
    mkdir -p "$RUN_DIR"
    
    # Set the mode to 777 temporarily to ensure the drop-privileged user CAN write the socket
    chmod 777 "$RUN_DIR"
    chown -R "$SERVICE_USER:$SERVICE_USER" "$CONFIG_DIR" "$LOG_DIR" "$RUN_DIR" "$LIB_DIR"
    
    # The Bridge
    ln -sf "$SM_SOCKET" /run/servicemanager.sock
    rm -f "$SM_SOCKET" /tmp/servicemanager.sock
}

generate_configs() {
    log "Generating fresh configurations in $CONFIG_DIR"

    # sm_daemon config (Maintained at /etc/servicemanager.conf for fallback support)
    cat > "/etc/servicemanager.conf" <<EOF
[socket]
path = $SM_SOCKET
mode = 0660
[server]
log_file = $LOG_DIR/servicemanager.log
max_services = 32
EOF
    cp "/etc/servicemanager.conf" "$CONFIG_DIR/servicemanager.conf"

    # Hardware Services
    for svc in audio camera gpio sensor; do
        cat > "$CONFIG_DIR/${svc}.ini" <<EOF
[servicemanager]
socket = $SM_SOCKET
[server]
log_file = $LOG_DIR/${svc}_service.log
EOF
    done

    # monitord config
    cat > "$CONFIG_DIR/monitord.ini" <<EOF
[daemon]
unix_socket_path = $MONITORD_SOCKET
http_port = $MONITORD_HTTP_PORT
EOF
    
    chown "$SERVICE_USER:$SERVICE_USER" "$CONFIG_DIR/"*
}

# ---------------------------------------------------------------------------
# 4. Lifecycle Management
# ---------------------------------------------------------------------------
start_process() {
    local name=$1; local bin=$2; shift 2
    log "Starting $name..."
    "$bin" "$@" &
    PIDS["$name"]=$!
}

wait_for_ready() {
    local path=$1; local timeout=10; local elapsed=0
    while [[ ! -S "$path" ]]; do
        sleep 0.5; elapsed=$((elapsed + 1))
        if (( elapsed > timeout * 2 )); then fail "$path never appeared"; return 1; fi
    done
    return 0
}

shutdown_all() {
    if [[ "$SHUTDOWN_IN_PROGRESS" == "true" ]]; then return; fi
    SHUTDOWN_IN_PROGRESS=true
    log "Initiating graceful shutdown..."
    for name in "${!PIDS[@]}"; do
        kill -TERM "${PIDS[$name]}" 2>/dev/null || true
    done
    sleep 1
    ok "All services stopped."
    exit 0
}

# ---------------------------------------------------------------------------
# 5. Main Loop
# ---------------------------------------------------------------------------
main() {
    if [[ "${EUID}" -ne 0 ]]; then fail "Must run as root."; exit 1; fi

    prepare_environment
    generate_configs

    # 1. Start Service Manager
    start_process "sm_daemon" "${INSTALL_DIR}/sbin/sm_daemon" --config "$CONFIG_DIR/servicemanager.conf"
    wait_for_ready "$SM_SOCKET" || exit 1

    # 2. Start Hardware Services
    for svc in audio camera gpio sensor; do
        if [[ -x "${INSTALL_DIR}/sbin/${svc}_service" ]]; then
            start_process "${svc}_service" "${INSTALL_DIR}/sbin/${svc}_service" --config "$CONFIG_DIR/${svc}.ini"
        fi
    done

    # 3. Start Monitor
    start_process "monitord" "${INSTALL_DIR}/sbin/monitord" --config "$CONFIG_DIR/monitord.ini"
    wait_for_ready "$MONITORD_SOCKET" || warn "Monitor starting slowly..."

    # 4. Interactive TUI
    if [[ "$LAUNCH_TUI" == "true" ]]; then
        trap shutdown_all INT TERM
        "${INSTALL_DIR}/bin/mw_tui" || true
        shutdown_all
    else
        log "Running in background. Use Ctrl+C to stop."
        wait
    fi
}

main "$@"
