#!/bin/bash
# Test Phase 3 CLI Middleware
# Professional command-line interface testing

echo "╔════════════════════════════════════════════════════════════════╗"
echo "║         Phase 3 Middleware CLI - Full Test Suite              ║"
echo "╚════════════════════════════════════════════════════════════════╝"
echo ""

CLI="./middleware-cli-x86"

# Test 1: Help command
echo "═══════════════════════════════════════════════════════════════"
echo "TEST 1: Help Commands"
echo "═══════════════════════════════════════════════════════════════"
echo ""
echo "$ middleware> help"
echo -e "help\nexit" | $CLI 2>&1 | tail -30
echo ""

# Test 2: Service Status
echo "═══════════════════════════════════════════════════════════════"
echo "TEST 2: Status - All Services"
echo "═══════════════════════════════════════════════════════════════"
echo ""
echo "$ middleware> status"
echo -e "status\nexit" | $CLI 2>&1 | tail -15
echo ""

# Test 3: Specific Service Status
echo "═══════════════════════════════════════════════════════════════"
echo "TEST 3: Status - Specific Service"
echo "═══════════════════════════════════════════════════════════════"
echo ""
echo "$ middleware> status capability-manager"
echo -e "status capability-manager\nexit" | $CLI 2>&1 | tail -15
echo ""

# Test 4: List Services
echo "═══════════════════════════════════════════════════════════════"
echo "TEST 4: List All Services"
echo "═══════════════════════════════════════════════════════════════"
echo ""
echo "$ middleware> list"
echo -e "list\nexit" | $CLI 2>&1 | tail -15
echo ""

# Test 5: Version
echo "═══════════════════════════════════════════════════════════════"
echo "TEST 5: Middleware Version"
echo "═══════════════════════════════════════════════════════════════"
echo ""
echo "$ middleware> version"
echo -e "version\nexit" | $CLI 2>&1 | tail -15
echo ""

# Test 6: Start/Stop/Restart
echo "═══════════════════════════════════════════════════════════════"
echo "TEST 6: Service Control (Start/Stop/Restart)"
echo "═══════════════════════════════════════════════════════════════"
echo ""
echo "$ middleware> stop logger"
echo "$ middleware> start logger"
echo "$ middleware> restart logger"
echo -e "stop logger\nstart logger\nrestart logger\nexit" | $CLI 2>&1 | tail -15
echo ""

# Test 7: Capabilities
echo "═══════════════════════════════════════════════════════════════"
echo "TEST 7: Capability Management"
echo "═══════════════════════════════════════════════════════════════"
echo ""
echo "$ middleware> capability myapp grant CAP_SERVICE_CALL"
echo "$ middleware> capability myapp revoke CAP_SERVICE_CALL"
echo -e "capability myapp grant CAP_SERVICE_CALL\ncapability myapp revoke CAP_SERVICE_CALL\nexit" | $CLI 2>&1 | tail -15
echo ""

# Test 8: Logs
echo "═══════════════════════════════════════════════════════════════"
echo "TEST 8: View Service Logs"
echo "═══════════════════════════════════════════════════════════════"
echo ""
echo "$ middleware> logs process-manager 3"
echo -e "logs process-manager 3\nexit" | $CLI 2>&1 | tail -15
echo ""

# Test 9: Statistics
echo "═══════════════════════════════════════════════════════════════"
echo "TEST 9: Performance Statistics"
echo "═══════════════════════════════════════════════════════════════"
echo ""
echo "$ middleware> stats"
echo -e "stats\nexit" | $CLI 2>&1 | tail -20
echo ""

# Test 10: Service Discovery
echo "═══════════════════════════════════════════════════════════════"
echo "TEST 10: Service Discovery"
echo "═══════════════════════════════════════════════════════════════"
echo ""
echo "$ middleware> discover"
echo -e "discover\nexit" | $CLI 2>&1 | tail -15
echo ""

# Test 11: Register Application
echo "═══════════════════════════════════════════════════════════════"
echo "TEST 11: Register New Application"
echo "═══════════════════════════════════════════════════════════════"
echo ""
echo "$ middleware> register myapp /usr/bin/myapp"
echo -e "register myapp /usr/bin/myapp\nexit" | $CLI 2>&1 | tail -10
echo ""

# Test 12: Monitor Service
echo "═══════════════════════════════════════════════════════════════"
echo "TEST 12: Monitor Service Activity (truncated)"
echo "═══════════════════════════════════════════════════════════════"
echo ""
echo "$ middleware> monitor event-dispatcher"
echo -e "monitor event-dispatcher\nexit" | $CLI 2>&1 | tail -15
echo ""

echo "═══════════════════════════════════════════════════════════════"
echo "✅ ALL TESTS COMPLETED"
echo "═══════════════════════════════════════════════════════════════"
echo ""
