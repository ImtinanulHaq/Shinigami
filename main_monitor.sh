#!/bin/bash
################################################################################
# main_monitor.sh — Complete Middleware Launcher & Monitor
################################################################################
# Starts all middleware services and launches the TUI monitoring dashboard.
# Usage: sudo ./main_monitor.sh
################################################################################

# Colors
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
BLUE='\033[0;34m'; CYAN='\033[0;36m'; MAGENTA='\033[0;35m'; NC='\033[0m'

WORKSPACE="/home/muhammad-imtinan-ul-haq/Desktop/middleware"

# ── Key binary paths ──────────────────────────────────────────────────────────
MONITOR_TUI="$WORKSPACE/dev/monitoring/middleware_monitor_tui"
SERVICES_BUILD="$WORKSPACE/dev/services/build"
SM_BIN="$WORKSPACE/servicemanager"

# Binaries inside the services build tree
AUDIO_BIN="$SERVICES_BUILD/audio_service/audio_service"
CAMERA_BIN="$SERVICES_BUILD/camera_service/camera_service"
GPIO_BIN="$SERVICES_BUILD/gpio_service/gpio_service"
SENSOR_BIN="$SERVICES_BUILD/sensor_service/sensor_service"

PIDS=()
MONITOR_PID=""

################################################################################
# Cleanup
################################################################################
cleanup() {
    echo ""
    echo -e "${YELLOW}═══════════════════════════════════════════════════════════${NC}"
    echo -e "${YELLOW} Shutting down all services...${NC}"
    echo -e "${YELLOW}═══════════════════════════════════════════════════════════${NC}"

    [ -n "$MONITOR_PID" ] && kill "$MONITOR_PID" 2>/dev/null || true

    for pid in "${PIDS[@]}"; do
        kill -0 "$pid" 2>/dev/null && kill "$pid" 2>/dev/null || true
    done

    pkill -x "audio_service"  2>/dev/null || true
    pkill -x "camera_service" 2>/dev/null || true
    pkill -x "sensor_service" 2>/dev/null || true
    pkill -x "gpio_service"   2>/dev/null || true
    pkill -f "servicemanager" 2>/dev/null || true

    # Remove mock symlinks
    rm -f /tmp/audio_service /tmp/camera_service /tmp/sensor_service /tmp/gpio_service 2>/dev/null || true
    # Clean sockets
    rm -f /run/servicemanager.sock /tmp/servicemanager.sock /tmp/middleware_monitor.sock 2>/dev/null || true

    echo -e "${GREEN}[✓] All services stopped${NC}"
    exit 0
}
trap cleanup EXIT INT TERM

################################################################################
# Banner
################################################################################
clear
echo -e "${MAGENTA}"
cat << "EOF"
╔══════════════════════════════════════════════════════════════════╗
║                                                                  ║
║        MIDDLEWARE MONITOR — Complete System Launcher            ║
║                                                                  ║
║  Steps:                                                          ║
║    1. Build monitor TUI (if needed)                              ║
║    2. Build service binaries (if needed)                         ║
║    3. Start Service Manager                                      ║
║    4. Start all 4 platform services                              ║
║    5. Launch TUI monitoring dashboard                            ║
║                                                                  ║
║  Press Ctrl+C to stop everything and exit                        ║
║                                                                  ║
╚══════════════════════════════════════════════════════════════════╝
EOF
echo -e "${NC}"

################################################################################
# [1/5] Validate environment
################################################################################
echo -e "${BLUE}[1/5] Validating environment...${NC}"

if [ "$EUID" -ne 0 ]; then
    echo -e "${RED}[✗] Must be run as root: sudo ./main_monitor.sh${NC}"
    exit 1
fi

if [ ! -d "$WORKSPACE" ]; then
    echo -e "${RED}[✗] Workspace not found: $WORKSPACE${NC}"
    exit 1
fi

cd "$WORKSPACE" || exit 1
echo -e "${GREEN}[✓] Environment OK${NC}"

################################################################################
# [2/5] Build monitor TUI (if missing)
################################################################################
echo -e "${BLUE}[2/5] Checking monitor TUI...${NC}"

if [ ! -f "$MONITOR_TUI" ]; then
    echo -e "${YELLOW}[!] monitor TUI not found — building...${NC}"
    gcc -O2 -Wall -o "$MONITOR_TUI" \
        "$WORKSPACE/dev/monitoring/middleware_monitor.c" \
        -lncurses -lpthread -lm
    if [ $? -eq 0 ]; then
        echo -e "${GREEN}[✓] Monitor TUI built: $MONITOR_TUI${NC}"
    else
        echo -e "${RED}[✗] Monitor TUI build failed${NC}"
        exit 1
    fi
else
    echo -e "${GREEN}[✓] Monitor TUI ready: $(du -sh "$MONITOR_TUI" | cut -f1) binary${NC}"
fi

################################################################################
# [3/5] Build service binaries (if missing)
################################################################################
echo -e "${BLUE}[3/5] Checking service binaries...${NC}"

SERVICES_NEED_BUILD=0
for bin in "$AUDIO_BIN" "$CAMERA_BIN" "$GPIO_BIN" "$SENSOR_BIN"; do
    [ ! -f "$bin" ] && SERVICES_NEED_BUILD=1 && break
done

if [ "$SERVICES_NEED_BUILD" -eq 1 ]; then
    echo -e "${YELLOW}[!] Service binaries not found — building...${NC}"
    cd "$WORKSPACE/dev/services" || exit 1
    cmake -B build -DCMAKE_BUILD_TYPE=Release -Wno-dev -DCMAKE_BUILD_TYPE=Release 2>&1 | tail -3
    cmake --build build -j"$(nproc)" 2>&1 | grep -E "Built target|Error" | head -20
    cd "$WORKSPACE" || exit 1
fi

# Confirm all 4 exist
ALL_OK=1
for bin in "$AUDIO_BIN" "$CAMERA_BIN" "$GPIO_BIN" "$SENSOR_BIN"; do
    if [ -f "$bin" ]; then
        echo -e "${GREEN}[✓] $(basename "$bin") binary ready${NC}"
    else
        echo -e "${YELLOW}[!] $(basename "$bin") binary not found — will use mock${NC}"
        ALL_OK=0
    fi
done
[ "$ALL_OK" -eq 1 ] && echo -e "${GREEN}[✓] All 4 service binaries ready${NC}"

################################################################################
# [4/5] Start Services
################################################################################
echo -e "${BLUE}[4/5] Starting services...${NC}"

# Clean previous state
pkill -x "audio_service" 2>/dev/null || true
pkill -x "camera_service" 2>/dev/null || true
pkill -x "sensor_service" 2>/dev/null || true
pkill -x "gpio_service" 2>/dev/null || true
pkill -f "servicemanager" 2>/dev/null || true
rm -f /run/servicemanager.sock /tmp/servicemanager.sock
# Remove stale PID files so duplicate-instance check doesn't block startup
rm -f /run/audio_service.pid /run/camera_service.pid /run/gpio_service.pid /run/sensor_service.pid
# Pre-create PID files as world-writable so non-root services can write them
for _svc in audio_service camera_service gpio_service sensor_service; do
    touch "/run/${_svc}.pid" 2>/dev/null && chmod 666 "/run/${_svc}.pid" 2>/dev/null || true
done
# Pre-create service log files as world-writable so non-root services can append
mkdir -p /var/log
for _svc in audio_service camera_service gpio_service sensor_service; do
    touch "/var/log/${_svc}.log" 2>/dev/null && chmod 666 "/var/log/${_svc}.log" 2>/dev/null || true
done
touch /var/log/servicemanager_audit.log 2>/dev/null || true
chmod 666 /var/log/servicemanager_audit.log 2>/dev/null || true
sleep 0.5

# Start Service Manager
if [ -f "$SM_BIN" ]; then
    "$SM_BIN" > /tmp/servicemanager.log 2>&1 &
    SM_PID=$!
    PIDS+=("$SM_PID")
    echo -e "${GREEN}[✓] Service Manager started (PID: $SM_PID)${NC}"
    # Wait for socket — explicit if/break to avoid || && precedence issues
    for i in $(seq 1 10); do
        if [ -S "/tmp/servicemanager.sock" ] || [ -S "/run/servicemanager.sock" ]; then
            echo -e "${GREEN}[✓] SM socket ready (attempt $i)${NC}"
            break
        fi
        echo -e "${CYAN}    Waiting for SM socket... ($i/10)${NC}"
        sleep 1
    done
    # Extra 1s for SM to complete all feature initialization
    sleep 1

    # Distribute SM HMAC key to all service key paths so services can sign messages
    SM_KEY=""
    if   [ -f "/run/servicemanager.key" ];  then SM_KEY="/run/servicemanager.key"
    elif [ -f "/tmp/servicemanager.key" ];  then SM_KEY="/tmp/servicemanager.key"
    fi
    if [ -n "$SM_KEY" ]; then
        for svc in audio_service camera_service gpio_service sensor_service; do
            mkdir -p "/etc/${svc}"
            cp "$SM_KEY" "/etc/${svc}/hmac.key"
            chmod 644 "/etc/${svc}/hmac.key"
        done
        echo -e "${GREEN}[✓] HMAC key distributed to all services (from $SM_KEY)${NC}"
    else
        echo -e "${YELLOW}[!] SM HMAC key not found — services will send unsigned messages${NC}"
    fi
else
    echo -e "${YELLOW}[!] servicemanager binary not found — monitor runs in stats-only mode${NC}"
fi

# Helper: start real binary or fall back to /bin/sleep mock (correct comm)
start_service() {
    local name="$1"
    local bin="$2"
    local conf="$WORKSPACE/dev/services/${name}/${name}.conf"

    if [ -f "$bin" ]; then
        # Run the real service binary with required config + foreground flag
        if [ -f "$conf" ]; then
            "$bin" -c "$conf" -f > "/tmp/${name}.log" 2>&1 &
        else
            "$bin" -f > "/tmp/${name}.log" 2>&1 &
        fi
        local pid=$!
        sleep 3  # give it 3 seconds to either stabilise or crash
        if kill -0 "$pid" 2>/dev/null; then
            PIDS+=("$pid")
            echo -e "${GREEN}[✓] $name running (PID: $pid) [real binary]${NC}"
            return
        else
            echo -e "${YELLOW}[!] $name real binary exited early — check /var/log/${name}.log${NC}"
            echo -e "${YELLOW}    Falling back to mock process${NC}"
        fi
    fi

    # Mock: symlink /bin/sleep so /proc/[pid]/comm matches the service name
    ln -sf /bin/sleep "/tmp/${name}"
    "/tmp/${name}" infinity &
    local pid=$!
    PIDS+=("$pid")
    echo -e "${YELLOW}[~] $name started (PID: $pid) [mock — real binary not running]${NC}"
}

start_service "audio_service"  "$AUDIO_BIN"
start_service "camera_service" "$CAMERA_BIN"
start_service "gpio_service"   "$GPIO_BIN"
start_service "sensor_service" "$SENSOR_BIN"

sleep 1

################################################################################
# Status summary
################################################################################
echo ""
echo -e "${MAGENTA}═══════════════════════════════════════════════════════════${NC}"
echo -e "${MAGENTA} Service Status Summary${NC}"
echo -e "${MAGENTA}═══════════════════════════════════════════════════════════${NC}"
echo ""

if [ -n "$SM_PID" ] && kill -0 "$SM_PID" 2>/dev/null; then
    echo -e "${GREEN}[✓] Service Manager   : Running (PID $SM_PID)${NC}"
else
    echo -e "${YELLOW}[!] Service Manager   : Not running${NC}"
fi

RUNNING=0
for pid in "${PIDS[@]}"; do
    kill -0 "$pid" 2>/dev/null && RUNNING=$((RUNNING+1))
done
echo -e "${GREEN}[✓] Services running  : $RUNNING / ${#PIDS[@]}${NC}"

if [ -S "/run/servicemanager.sock" ]; then
    echo -e "${GREEN}[✓] SM Socket         : /run/servicemanager.sock${NC}"
elif [ -S "/tmp/servicemanager.sock" ]; then
    echo -e "${GREEN}[✓] SM Socket         : /tmp/servicemanager.sock${NC}"
else
    echo -e "${YELLOW}[!] SM Socket         : not found${NC}"
fi

echo ""
echo -e "${CYAN}PIDs: ${PIDS[*]}${NC}"
echo ""

################################################################################
# [5/5] Launch TUI
################################################################################
echo -e "${BLUE}[5/5] Launching Monitor Dashboard...${NC}"
echo ""
echo -e "${MAGENTA}═══════════════════════════════════════════════════════════${NC}"
echo -e "${MAGENTA} TUI Controls:${NC}"
echo -e "${MAGENTA}   Q / ESC      — Quit (stops all services)${NC}"
echo -e "${MAGENTA}   Tab / →      — Next panel${NC}"
echo -e "${MAGENTA}   ←            — Previous panel${NC}"
echo -e "${MAGENTA}   0-9          — Jump to panel by number${NC}"
echo -e "${MAGENTA}   r            — Force refresh${NC}"
echo -e "${MAGENTA}   F1           — Help screen (all 15 panels)${NC}"
echo -e "${MAGENTA}═══════════════════════════════════════════════════════════${NC}"
sleep 2

export LANG=en_US.UTF-8
export LC_ALL=en_US.UTF-8

# Launch TUI — blocks until user quits
MONITOR_PID=""
"$MONITOR_TUI"

# cleanup() auto-called on exit
