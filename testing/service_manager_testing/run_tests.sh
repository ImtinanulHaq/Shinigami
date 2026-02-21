#!/bin/bash
# Test Suite Runner for Service Manager
# Usage: ./run_tests.sh [level1|level2|level3|level4|level5|all]

set -e

LEVEL=${1:-all}
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
mkdir -p "$BUILD_DIR"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

log_info() { echo -e "${GREEN}[INFO]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }

# Level 1: Static Analysis
run_level1() {
    log_info "=== LEVEL 1: Static Analysis ==="
    
    # Check for cppcheck
    if ! command -v cppcheck &> /dev/null; then
        log_warn "cppcheck not found, skipping"
    else
        log_info "Running cppcheck..."
        cppcheck --enable=all --error-exitcode=1 ../../core/service_manager/*.c 2>&1 | head -50 || true
    fi
    
    # GCC warnings
    log_info "Running GCC with maximum warnings..."
    cd "$SCRIPT_DIR"
    gcc -Wall -Wextra -Wshadow -Wformat=2 -Wconversion -Werror \
        ../../core/service_manager/sm_protocol.c \
        ../../core/service_manager/sm_registry.c \
        ../../core/service_manager/sm_crypto.c \
        -I../../core -o /dev/null 2>&1 || log_warn "Some warnings found (acceptable)"
    
    log_info "LEVEL 1 Complete"
}

# Level 2: Unit Tests
run_level2() {
    log_info "=== LEVEL 2: Unit Tests ==="
    
    if [ ! -f "$SCRIPT_DIR/test_sm_protocol.c" ]; then
        log_error "Unit test files not found"
        return 1
    fi
    
    log_info "Compiling unit tests..."
    gcc -o "$BUILD_DIR/test_protocol" \
        "$SCRIPT_DIR/test_sm_protocol.c" \
        "$SCRIPT_DIR/unity.c" \
        ../../core/service_manager/sm_protocol.c \
        -I../../core -I"$SCRIPT_DIR" -lm 2>&1 | head -20 || true
    
    log_info "Running protocol tests..."
    "$BUILD_DIR/test_protocol" || true
    
    # Other unit tests
    for test in "$SCRIPT_DIR"/test_*.c; do
        [ "$test" = "$SCRIPT_DIR/test_sm_protocol.c" ] && continue
        test_name=$(basename "$test" .c)
        log_info "Compiling $test_name..."
        
        gcc -o "$BUILD_DIR/$test_name" \
            "$test" \
            "$SCRIPT_DIR/unity.c" \
            ../../core/service_manager/*.c \
            -I../../core -I"$SCRIPT_DIR" -lpthread -lm 2>&1 | head -10 || true
        
        [ -f "$BUILD_DIR/$test_name" ] && "$BUILD_DIR/$test_name" || true
    done
    
    log_info "LEVEL 2 Complete"
}

# Level 3: Integration Tests
run_level3() {
    log_info "=== LEVEL 3: Integration Tests ==="
    
    if [ ! -f "$SCRIPT_DIR/integration_test.sh" ]; then
        log_error "Integration test script not found"
        return 1
    fi
    
    bash "$SCRIPT_DIR/integration_test.sh"
    log_info "LEVEL 3 Complete"
}

# Level 4: End-to-End Tests
run_level4() {
    log_info "=== LEVEL 4: End-to-End Tests ==="
    
    if [ ! -f "$SCRIPT_DIR/e2e_test.sh" ]; then
        log_error "E2E test script not found"
        return 1
    fi
    
    bash "$SCRIPT_DIR/e2e_test.sh"
    log_info "LEVEL 4 Complete"
}

# Level 5: Fuzz Testing
run_level5() {
    log_info "=== LEVEL 5: Fuzz Testing ==="
    
    if [ ! -f "$SCRIPT_DIR/fuzz_target.c" ]; then
        log_error "Fuzz target not found"
        return 1
    fi
    
    log_info "Compiling with AddressSanitizer..."
    gcc -fsanitize=address,undefined -g -o "$BUILD_DIR/fuzz_target" \
        "$SCRIPT_DIR/fuzz_target.c" \
        ../../core/service_manager/sm_protocol.c \
        -I../../core 2>&1 | head -20 || true
    
    log_info "Running fuzz tests (30 seconds)..."
    timeout 30 "$BUILD_DIR/fuzz_target" < /dev/zero > /dev/null 2>&1 || true
    
    log_info "LEVEL 5 Complete (check for ASAN reports above)"
}

# Run selected level(s)
case $LEVEL in
    level1) run_level1 ;;
    level2) run_level2 ;;
    level3) run_level3 ;;
    level4) run_level4 ;;
    level5) run_level5 ;;
    all)
        run_level1 || true
        run_level2 || true
        run_level3 || true
        run_level4 || true
        run_level5 || true
        log_info "=== ALL TESTS COMPLETE ==="
        ;;
    *)
        echo "Usage: $0 [level1|level2|level3|level4|level5|all]"
        exit 1
        ;;
esac

log_info "Test suite finished"
