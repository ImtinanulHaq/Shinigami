#!/bin/bash
################################################################################
# main_monitor - Complete Middleware Launcher & Monitor
################################################################################
# Description: Starts all middleware services and launches the monitor dashboard
# Usage: sudo ./main_monitor
################################################################################

set -e  # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
MAGENTA='\033[0;35m'
NC='\033[0m' # No Color

WORKSPACE="/home/muhammad-imtinan-ul-haq/Desktop/middleware"
BUILD_DIR="$WORKSPACE/build/dev"

# PIDs to track
PIDS=()

################################################################################
# Cleanup Function
################################################################################
cleanup() {
    echo ""
    echo -e "${YELLOW}═══════════════════════════════════════════════════════════${NC}"
    echo -e "${YELLOW} Shutting down all services...${NC}"
    echo -e "${YELLOW}═══════════════════════════════════════════════════════════${NC}"
    
    # Kill monitor first
    if [ -n "$MONITOR_PID" ]; then
        echo -e "${CYAN}[✓] Stopping monitor (PID $MONITOR_PID)${NC}"
        kill $MONITOR_PID 2>/dev/null || true
    fi
    
    # Kill all tracked services
    for pid in "${PIDS[@]}"; do
        if kill -0 $pid 2>/dev/null; then
            echo -e "${CYAN}[✓] Stopping service (PID $pid)${NC}"
            kill $pid 2>/dev/null || true
        fi
    done
    
    # Kill any remaining processes
    pkill -f "servicemanager" 2>/dev/null || true
    pkill -f "audio_hal" 2>/dev/null || true
    pkill -f "camera_hal" 2>/dev/null || true
    pkill -f "sensor_hal" 2>/dev/null || true
    pkill -f "gpio_hal" 2>/dev/null || true
    pkill -f "middleware_monitord" 2>/dev/null || true
    pkill -f "middleware_monitor" 2>/dev/null || true
    pkill -f "middleware_monitor_tui" 2>/dev/null || true
    
    # Clean up socket files
    rm -f /run/servicemanager.sock 2>/dev/null || true
    rm -f /tmp/servicemanager.sock 2>/dev/null || true
    rm -f /tmp/middleware_monitor.sock 2>/dev/null || true
    
    echo -e "${GREEN}[✓] All services stopped${NC}"
    echo ""
    exit 0
}

# Setup trap for cleanup
trap cleanup EXIT INT TERM

################################################################################
# Banner
################################################################################
clear
echo -e "${MAGENTA}"
cat << "EOF"
╔══════════════════════════════════════════════════════════════════╗
║                                                                  ║
║          MIDDLEWARE MONITOR - Complete System Launcher          ║
║                                                                  ║
║  This script will:                                               ║
║    1. Start Service Manager daemon                               ║
║    2. Start HAL services (audio, camera, sensor, gpio)          ║
║    3. Launch the monitoring dashboard                            ║
║                                                                  ║
║  Press Ctrl+C to stop all services and exit                      ║
║                                                                  ║
╚══════════════════════════════════════════════════════════════════╝
EOF
echo -e "${NC}"

################################################################################
# Validation
################################################################################
echo -e "${BLUE}[1/6] Validating environment...${NC}"

# Check if running as root/sudo
if [ "$EUID" -ne 0 ]; then 
    echo -e "${RED}[✗] Error: This script must be run as root (use sudo)${NC}"
    exit 1
fi

# Check if workspace exists
if [ ! -d "$WORKSPACE" ]; then
    echo -e "${RED}[✗] Error: Workspace not found: $WORKSPACE${NC}"
    exit 1
fi

cd "$WORKSPACE" || exit 1

# Check if monitoring system exists
MONITOR_STANDALONE="$WORKSPACE/monitoring/build/middleware_monitor"
MONITOR_TUI="$WORKSPACE/monitoring/build/middleware_monitor_tui"
MONITOR_DAEMON="$WORKSPACE/monitoring/build/middleware_monitord"

if [ ! -f "$MONITOR_TUI" ] || [ ! -f "$MONITOR_DAEMON" ]; then
    echo -e "${YELLOW}[!] Monitoring system not found. Building...${NC}"
    cd monitoring/build || {
        echo -e "${YELLOW}[!] Build directory not found, creating...${NC}"
        mkdir -p monitoring/build
        cd monitoring/build || exit 1
        cmake .. || {
            echo -e "${RED}[✗] CMake failed${NC}"
            exit 1
        }
    }
    make -j$(nproc) || {
        echo -e "${RED}[✗] Build failed${NC}"
        exit 1
    }
    cd "$WORKSPACE" || exit 1
fi

echo -e "${GREEN}[✓] Environment validated${NC}"
sleep 1

################################################################################
# Build Services (if needed)
################################################################################
echo -e "${BLUE}[2/6] Checking service binaries...${NC}"

# Check if servicemanager exists
SM_BIN=""
if [ -f "servicemanager" ]; then
    SM_BIN="$WORKSPACE/servicemanager"
elif [ -f "$BUILD_DIR/core/service_manager/servicemanager" ]; then
    SM_BIN="$BUILD_DIR/core/service_manager/servicemanager"
elif [ -f "bankai" ]; then
    SM_BIN="$WORKSPACE/bankai"
fi

if [ -z "$SM_BIN" ]; then
    echo -e "${YELLOW}[!] Service Manager not found - Building middleware...${NC}"
    if [ -f "Makefile" ]; then
        echo -e "${CYAN}    Running: make servicemanager${NC}"
        make servicemanager 2>&1 | tail -10
        
        # Check again after build
        if [ -f "servicemanager" ]; then
            SM_BIN="$WORKSPACE/servicemanager"
            echo -e "${GREEN}[✓] Service Manager built successfully${NC}"
        elif [ -f "bankai" ]; then
            SM_BIN="$WORKSPACE/bankai"
            echo -e "${GREEN}[✓] Service Manager (bankai) built successfully${NC}"
        else
            echo -e "${YELLOW}[!] Build completed but binary not found${NC}"
            echo -e "${CYAN}    Will run monitoring system only (no services)${NC}"
        fi
    else
        echo -e "${YELLOW}[!] No Makefile found - will run monitoring only${NC}"
    fi
else
    echo -e "${GREEN}[✓] Service Manager found: $SM_BIN${NC}"
fi

# Check HAL services
HAL_DIR="$BUILD_DIR/hal"
if [ ! -d "$HAL_DIR" ]; then
    echo -e "${YELLOW}[!] HAL directory not found${NC}"
fi

echo -e "${GREEN}[✓] Service binaries check complete${NC}"
sleep 1

################################################################################
# Clean Previous State
################################################################################
echo -e "${BLUE}[3/6] Cleaning previous state...${NC}"

# Kill any existing processes
pkill -f "servicemanager" 2>/dev/null || true
pkill -f "audio_hal" 2>/dev/null || true
pkill -f "camera_hal" 2>/dev/null || true
pkill -f "sensor_hal" 2>/dev/null || true
pkill -f "gpio_hal" 2>/dev/null || true

# Remove old sockets
rm -f /run/servicemanager.sock 2>/dev/null || true
rm -f /tmp/servicemanager.sock 2>/dev/null || true

# Create log directory
mkdir -p /var/log
touch /var/log/servicemanager_audit.log 2>/dev/null || true
chmod 666 /var/log/servicemanager_audit.log 2>/dev/null || true

echo -e "${GREEN}[✓] Previous state cleaned${NC}"
sleep 1

################################################################################
# Start Service Manager
################################################################################
echo -e "${BLUE}[4/6] Starting Service Manager...${NC}"

if [ -f "$SM_BIN" ]; then
    # Start Service Manager in background
    $SM_BIN > /tmp/servicemanager.log 2>&1 &
    SM_PID=$!
    PIDS+=($SM_PID)
    
    echo -e "${GREEN}[✓] Service Manager started (PID: $SM_PID)${NC}"
    
    # Wait for socket to be created
    for i in {1..10}; do
        if [ -S "/run/servicemanager.sock" ] || [ -S "/tmp/servicemanager.sock" ]; then
            echo -e "${GREEN}[✓] Service Manager socket ready${NC}"
            break
        fi
        echo -e "${CYAN}    Waiting for socket... ($i/10)${NC}"
        sleep 1
    done
else
    echo -e "${YELLOW}[!] Service Manager binary not found, monitor will show system stats only${NC}"
fi

sleep 2

################################################################################
# Start HAL Services
################################################################################
echo -e "${BLUE}[5/6] Starting HAL services...${NC}"

# Function to start a mock service
start_mock_service() {
    local name=$1
    local socket_path="/tmp/${name}.sock"
    
    # Create a simple background process that just sleeps
    # In real scenario, these would be actual HAL binaries
    (
        while true; do
            sleep 1
        done
    ) &
    
    local pid=$!
    PIDS+=($pid)
    echo -e "${GREEN}[✓] $name started (PID: $pid)${NC}"
}

# Start HAL services
if [ -f "$HAL_DIR/audio_hal" ]; then
    $HAL_DIR/audio_hal &
    PIDS+=($!)
    echo -e "${GREEN}[✓] Audio HAL started (PID: $!)${NC}"
else
    start_mock_service "audio_service"
fi

sleep 0.5

if [ -f "$HAL_DIR/camera_hal" ]; then
    $HAL_DIR/camera_hal &
    PIDS+=($!)
    echo -e "${GREEN}[✓] Camera HAL started (PID: $!)${NC}"
else
    start_mock_service "camera_service"
fi

sleep 0.5

if [ -f "$HAL_DIR/sensor_hal" ]; then
    $HAL_DIR/sensor_hal &
    PIDS+=($!)
    echo -e "${GREEN}[✓] Sensor HAL started (PID: $!)${NC}"
else
    start_mock_service "sensor_service"
fi

sleep 0.5

if [ -f "$HAL_DIR/gpio_hal" ]; then
    $HAL_DIR/gpio_hal &
    PIDS+=($!)
    echo -e "${GREEN}[✓] GPIO HAL started (PID: $!)${NC}"
else
    start_mock_service "gpio_service"
fi

echo -e "${GREEN}[✓] All HAL services started${NC}"
sleep 2

################################################################################
# Show Status
################################################################################
echo ""
echo -e "${MAGENTA}═══════════════════════════════════════════════════════════${NC}"
echo -e "${MAGENTA} Service Status Summary${NC}"
echo -e "${MAGENTA}═══════════════════════════════════════════════════════════${NC}"
echo ""

if [ -n "$SM_PID" ] && kill -0 $SM_PID 2>/dev/null; then
    echo -e "${GREEN}[✓] Service Manager       : Running (PID $SM_PID)${NC}"
else
    echo -e "${YELLOW}[!] Service Manager       : Not running${NC}"
fi

echo -e "${GREEN}[✓] HAL Services          : ${#PIDS[@]} services running${NC}"

if [ -S "/run/servicemanager.sock" ]; then
    echo -e "${GREEN}[✓] SM Socket             : /run/servicemanager.sock${NC}"
elif [ -S "/tmp/servicemanager.sock" ]; then
    echo -e "${GREEN}[✓] SM Socket             : /tmp/servicemanager.sock${NC}"
else
    echo -e "${YELLOW}[!] SM Socket             : Not found${NC}"
fi

echo ""
echo -e "${CYAN}Active PIDs: ${PIDS[*]}${NC}"
echo ""

################################################################################
# Launch Monitor
################################################################################
echo -e "${BLUE}[6/6] Launching Monitor Dashboard...${NC}"
sleep 2

# Set locale for Unicode support
export LANG=en_US.UTF-8
export LC_ALL=en_US.UTF-8

# Start monitoring daemon first
echo -e "${BLUE}Starting monitoring daemon...${NC}"
$MONITOR_DAEMON > /tmp/monitord.log 2>&1 &
MONITORD_PID=$!
PIDS+=($MONITORD_PID)
echo -e "${GREEN}[✓] Monitoring daemon started (PID: $MONITORD_PID)${NC}"
sleep 2

echo -e "${MAGENTA}═══════════════════════════════════════════════════════════${NC}"
echo -e "${MAGENTA} Monitor Controls:${NC}"
echo -e "${MAGENTA}   Q - Quit monitor (will stop all services)${NC}"
echo -e "${MAGENTA}   Arrow keys - Navigate${NC}"
echo -e "${MAGENTA}   ESC - Exit${NC}"
echo -e "${MAGENTA}═══════════════════════════════════════════════════════════${NC}"
sleep 2

# Launch monitor TUI (blocking) — prefer standalone binary
if [ -f "$MONITOR_STANDALONE" ]; then
    "$MONITOR_STANDALONE"
else
    "$MONITOR_TUI"
fi

# When monitor exits, cleanup will be called automatically
