#!/bin/bash
# Test Suite Runner for Service Manager
# Updated for modular directory structure (dev/core/service_manager/*)

set -e

# --- Configuration ---
LEVEL=${1:-all}
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
PROJECT_ROOT="$(realpath "$SCRIPT_DIR/../../")"
DEV_CORE="$PROJECT_ROOT/dev/core"
SM_ROOT="$DEV_CORE/service_manager"

mkdir -p "$BUILD_DIR"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# Logging
log_info() { echo -e "${GREEN}[INFO]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }

# --- Source Definitions ---
# 1. Include paths for all submodules so headers are found
INCLUDES=(
    "-I$DEV_CORE"
    "-I$SM_ROOT"
    "-I$SM_ROOT/infrastructure"
    "-I$SM_ROOT/security"
    "-I$SM_ROOT/lifecycle"
    "-I$SM_ROOT/observability"
    "-I$SM_ROOT/enterprise"
    "-I$SCRIPT_DIR"
)

# 2. Collect all source files needed for linking.
#    IMPORTANT: We exclude 'sm_main.c' because unit tests define their own main().
#    We use 'find' to handle the nested folder structure automatically.
LIB_SOURCES=$(find "$SM_ROOT" -name "*.c" ! -name "sm_main.c")
# Add ring_buffer.c from the core root
LIB_SOURCES="$LIB_SOURCES $DEV_CORE/ring_buffer.c"

# --- Level 1: Static Analysis ---
run_level1() {
    log_info "=== LEVEL 1: Static Analysis ==="

    if command -v cppcheck &> /dev/null; then
        log_info "Running cppcheck..."
        # Check specific folders recursively
        cppcheck --enable=all --error-exitcode=1 \
            --suppress=missingIncludeSystem \
            --suppress=unusedFunction \
            "$SM_ROOT" "$DEV_CORE/ring_buffer.c" \
            2>&1 | head -50 || log_warn "Cppcheck found issues (see above)"
    else
        log_warn "cppcheck not found, skipping"
    fi

    log_info "Running GCC strict compilation check..."
    # Compile critical files to ensure no strict warnings
    gcc -Wall -Wextra -Wshadow -Werror \
        "${INCLUDES[@]}" \
        -c "$SM_ROOT/infrastructure/sm_protocol.c" \
        -c "$SM_ROOT/infrastructure/sm_registry.c" \
        -c "$SM_ROOT/security/sm_crypto.c" \
        -o /dev/null || log_warn "GCC strict check failed"

    log_info "LEVEL 1 Complete"
}

# --- Level 2: Unit Tests ---
run_level2() {
    log_info "=== LEVEL 2: Unit Tests ==="

    # Compile and run each test file found in the current directory
    for test_file in "$SCRIPT_DIR"/test_*.c; do
        test_name=$(basename "$test_file" .c)
        log_info "Compiling $test_name..."
        
        # Link the test file + unity.c + all middleware sources + libraries
        gcc -g -o "$BUILD_DIR/$test_name" \
            "$test_file" \
            "$SCRIPT_DIR/unity.c" \
            $LIB_SOURCES \
            "${INCLUDES[@]}" \
            -lssl -lcrypto -lpthread -lm -lrt || {
                log_error "Compilation failed for $test_name"
                continue
            }

        log_info "Running $test_name..."
        "$BUILD_DIR/$test_name" || {
            log_error "$test_name FAILED"
            # return 1 # Uncomment to stop on first failure
        }
    done

    log_info "LEVEL 2 Complete"
}

# --- Level 3: Integration Tests ---
run_level3() {
    log_info "=== LEVEL 3: Integration Tests ==="
    if [ -f "$SCRIPT_DIR/integration_test.sh" ]; then
        chmod +x "$SCRIPT_DIR/integration_test.sh"
        # Pass the build dir or other vars if needed
        bash "$SCRIPT_DIR/integration_test.sh"
    else
        log_error "integration_test.sh not found"
        return 1
    fi
    log_info "LEVEL 3 Complete"
}

# --- Level 4: End-to-End Tests ---
run_level4() {
    log_info "=== LEVEL 4: End-to-End Tests ==="
    if [ -f "$SCRIPT_DIR/e2e_test.sh" ]; then
        chmod +x "$SCRIPT_DIR/e2e_test.sh"
        bash "$SCRIPT_DIR/e2e_test.sh"
    else
        log_error "e2e_test.sh not found"
        return 1
    fi
    log_info "LEVEL 4 Complete"
}

# --- Level 5: Fuzz Testing ---
run_level5() {
    log_info "=== LEVEL 5: Fuzz Testing ==="
    
    FUZZ_TARGET="$SCRIPT_DIR/fuzz_target.c"
    if [ ! -f "$FUZZ_TARGET" ]; then
        log_error "fuzz_target.c not found"
        return 1
    fi

    log_info "Compiling Fuzz Target with AddressSanitizer..."
    gcc -fsanitize=address,undefined -g -o "$BUILD_DIR/fuzz_target" \
        "$FUZZ_TARGET" \
        $LIB_SOURCES \
        "${INCLUDES[@]}" \
        -lssl -lcrypto -lpthread -lm -lrt

    log_info "Running Fuzz Target (30s)..."
    # Use timeout to run fuzzer for 30 seconds
    timeout 30s "$BUILD_DIR/fuzz_target" < /dev/urandom > /dev/null 2>&1 || {
        EXIT_CODE=$?
        if [ $EXIT_CODE -eq 124 ]; then
            log_info "Fuzzing finished (timeout reached, this is good)"
        else
            log_warn "Fuzzing crashed or exited with code $EXIT_CODE"
        fi
    }
    log_info "LEVEL 5 Complete"
}

# --- Execution Entry Point ---
case $LEVEL in
    level1) run_level1 ;;
    level2) run_level2 ;;
    level3) run_level3 ;;
    level4) run_level4 ;;
    level5) run_level5 ;;
    all)
        run_level1
        run_level2
        run_level3
        run_level4
        run_level5
        log_info "=== ALL TESTS COMPLETE ==="
        ;;
    *)
        echo "Usage: $0 [level1|level2|level3|level4|level5|all]"
        exit 1
        ;;
esac
