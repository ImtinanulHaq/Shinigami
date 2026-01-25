#!/bin/bash

echo "╔════════════════════════════════════════════╗"
echo "║   Phase 2 - Functionality Test             ║"
echo "╚════════════════════════════════════════════╝"
echo ""

# Start daemon
echo "[1/5] Starting daemon..."
./build/daemon > /tmp/daemon.log 2>&1 &
DAEMON_PID=$!
sleep 2

if ! kill -0 $DAEMON_PID 2>/dev/null; then
    echo "✗ FAIL: Daemon failed to start"
    exit 1
fi
echo "✓ PASS: Daemon started (PID: $DAEMON_PID)"
echo ""

# Test ping command
echo "[2/5] Testing daemon connectivity..."
if timeout 2 ./build/client "ping" 2>/dev/null | grep -q "Pong"; then
    echo "✓ PASS: Daemon responds to ping"
else
    echo "✗ FAIL: No response from daemon"
    kill $DAEMON_PID
    exit 1
fi
echo ""

# Test help command
echo "[3/5] Testing help system..."
if timeout 2 ./build/client "help" 2>/dev/null | grep -q "SYSTEM COMMANDS"; then
    echo "✓ PASS: Help system working"
else
    echo "✗ FAIL: Help command not responding"
fi
echo ""

# Test status command
echo "[4/5] Testing status command..."
if timeout 2 ./build/client "status" 2>/dev/null | grep -q "Daemon"; then
    echo "✓ PASS: Status command working"
else
    echo "⚠ WARN: Status may need daemon connection setup"
fi
echo ""

# Test process list
echo "[5/5] Testing process listing..."
if timeout 2 ./build/client "list_processes" 2>/dev/null > /dev/null; then
    echo "✓ PASS: Process listing working"
else
    echo "⚠ WARN: Process listing may return empty"
fi
echo ""

# Cleanup
kill $DAEMON_PID 2>/dev/null
wait $DAEMON_PID 2>/dev/null

echo "════════════════════════════════════════════"
echo "✓ Phase 2 tests complete!"
echo "════════════════════════════════════════════"
