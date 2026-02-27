#!/bin/bash
# Service Manager Comprehensive Test Harness (Levels 1-5)
# Professional production-grade testing framework
# Updated for Modular Directory Structure (dev/core/service_manager)

# Don't use set -e because we need to handle test failures gracefully
# Instead, we explicitly check exit codes in tests

# --- Configuration & Path Resolution ---
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(realpath "$SCRIPT_DIR/../../")"
BUILD_DIR="$SCRIPT_DIR/build"
DEV_CORE="$PROJECT_ROOT/dev/core"
SM_ROOT="$DEV_CORE/service_manager"

mkdir -p "$BUILD_DIR"

# --- Source File Discovery ---
# We define these variables here so all levels can use them.
# 1. Compiler Includes
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

# 2. Source Files (Excluding sm_main.c to avoid main() conflicts)
LIB_SOURCES=$(find "$SM_ROOT" -name "*.c" ! -name "sm_main.c")
LIB_SOURCES="$LIB_SOURCES $DEV_CORE/ring_buffer.c"

# --- Colors & Logging ---
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
MAGENTA='\033[0;35m'
NC='\033[0m'

TOTAL_TESTS=0
TOTAL_PASSED=0
TOTAL_FAILED=0

log_level() { echo -e "${MAGENTA}[Level $1]${NC} $2"; }
log_pass()  { echo -e "${GREEN}✓${NC} $1"; }
log_fail()  { echo -e "${RED}✗${NC} $1"; }
log_info()  { echo -e "${YELLOW}[INFO]${NC} $1"; }

header() {
    echo ""
    echo "========================================="
    echo "$1"
    echo "========================================="
    echo ""
}

# --- Level 1: Static Analysis ---
run_level_1_static_analysis() {
    header "Level 1: Static Analysis"
    local passed=0
    local failed=0
    
    # 1. Cppcheck
    if command -v cppcheck >/dev/null 2>&1; then
        log_level "1" "Running cppcheck..."
        if cppcheck --enable=all --error-exitcode=1 \
            --suppress=missingIncludeSystem \
            --suppress=unusedFunction \
            "$SM_ROOT" "$DEV_CORE/ring_buffer.c" 2>&1 | head -50; then
            ((passed++))
            log_pass "Cppcheck passed"
        else
            ((failed++))
            log_fail "Cppcheck found issues"
        fi
    else
        log_info "cppcheck not found, skipping"
    fi

    # 2. GCC Strict Warning Check
    log_level "1" "Running GCC strict compilation check..."
    if gcc -Wall -Wextra -Wshadow -Werror "${INCLUDES[@]}" \
       -c "$SM_ROOT/infrastructure/sm_protocol.c" \
       -c "$SM_ROOT/security/sm_crypto.c" -o /dev/null 2>/dev/null; then
        ((passed++))
        log_pass "Strict GCC compilation passed"
    else
        ((failed++))
        log_fail "Strict GCC compilation failed (warnings treated as errors)"
    fi
    
    echo ""
    echo -e "Level 1: ${GREEN}$passed${NC} passed, ${RED}$failed${NC} failed"
    TOTAL_PASSED=$((TOTAL_PASSED + passed))
    TOTAL_FAILED=$((TOTAL_FAILED + failed))
}

# --- Level 2: Unit Tests ---
run_level_2_unit_tests() {
    header "Level 2: Unit Tests (Protocol, Registry, Crypto, Rate Limit)"
    local passed=0
    local failed=0
    
    log_level "2" "Compiling and running tests..."
    
    for test_file in "$SCRIPT_DIR"/test_*.c; do
        test_name=$(basename "$test_file" .c)
        
        # Compile
        if gcc -g -o "$BUILD_DIR/$test_name" \
            "$test_file" "$SCRIPT_DIR/unity.c" \
            $LIB_SOURCES "${INCLUDES[@]}" \
            -lssl -lcrypto -lpthread -lm -lrt; then
            
            # Run
            if "$BUILD_DIR/$test_name"; then
                ((passed++))
                log_pass "$test_name passed"
            else
                ((failed++))
                log_fail "$test_name failed at runtime"
            fi
        else
            ((failed++))
            log_fail "$test_name failed to compile"
        fi
    done
    
    echo ""
    echo -e "Level 2: ${GREEN}$passed${NC} passed, ${RED}$failed${NC} failed"
    TOTAL_PASSED=$((TOTAL_PASSED + passed))
    TOTAL_FAILED=$((TOTAL_FAILED + failed))
}

# --- Level 3: Integration Tests ---
run_level_3_integration() {
    header "Level 3: Integration Tests"
    local passed=0
    local failed=0
    
    TEST_SCRIPT="$SCRIPT_DIR/integration_test.sh"
    
    if [ -x "$TEST_SCRIPT" ]; then
        log_level "3" "Running integration_test.sh"
        if "$TEST_SCRIPT"; then
            ((passed++))
            log_pass "Integration tests passed"
        else
            ((failed++))
            log_fail "Integration tests failed"
        fi
    else
        log_fail "integration_test.sh not found or not executable"
        ((failed++))
    fi
    
    echo ""
    echo -e "Level 3: ${GREEN}$passed${NC} passed, ${RED}$failed${NC} failed"
    TOTAL_PASSED=$((TOTAL_PASSED + passed))
    TOTAL_FAILED=$((TOTAL_FAILED + failed))
}

# --- Level 4: E2E Tests ---
run_level_4_e2e() {
    header "Level 4: End-to-End Tests"
    local passed=0
    local failed=0
    
    TEST_SCRIPT="$SCRIPT_DIR/e2e_test.sh"
    
    if [ -x "$TEST_SCRIPT" ]; then
        log_level "4" "Running e2e_test.sh"
        if "$TEST_SCRIPT"; then
            ((passed++))
            log_pass "E2E tests passed"
        else
            ((failed++))
            log_fail "E2E tests failed"
        fi
    else
        log_fail "e2e_test.sh not found or not executable"
        ((failed++))
    fi
    
    echo ""
    echo -e "Level 4: ${GREEN}$passed${NC} passed, ${RED}$failed${NC} failed"
    TOTAL_PASSED=$((TOTAL_PASSED + passed))
    TOTAL_FAILED=$((TOTAL_FAILED + failed))
}

# --- Level 5: Fuzz Testing ---
run_level_5_fuzz() {
    header "Level 5: Fuzz Testing (ASAN, UBSAN)"
    local passed=0
    local failed=0
    
    FUZZ_SRC="$SCRIPT_DIR/fuzz_target.c"
    
    if [ ! -f "$FUZZ_SRC" ]; then
        log_info "fuzz_target.c not found, skipping"
        return
    fi
    
    # 1. Build ASAN Target
    log_level "5" "Building ASAN instrumented fuzz target..."
    if gcc -fsanitize=address,undefined -g -o "$BUILD_DIR/fuzz_target_asan" \
        "$FUZZ_SRC" $LIB_SOURCES "${INCLUDES[@]}" \
        -lssl -lcrypto -lpthread -lm -lrt; then
        ((passed++))
        log_pass "ASAN target built"
        
        # 2. Dry Run (30s)
        log_level "5" "Running 30s dry run..."
        if timeout 30s "$BUILD_DIR/fuzz_target_asan" < /dev/urandom > /dev/null 2>&1; then
             # timeout exit code 124 is normal/pass for us here
             : 
        fi
        # Check if it crashed (exit code > 128 usually signal)
        if [ $? -lt 128 ]; then
             log_pass "Dry run survived"
        else
             log_fail "Dry run crashed"
        fi
    else
        ((failed++))
        log_fail "Failed to build ASAN target"
    fi
    
    echo ""
    echo -e "Level 5: ${GREEN}$passed${NC} targets built/ran"
    TOTAL_PASSED=$((TOTAL_PASSED + passed))
    TOTAL_FAILED=$((TOTAL_FAILED + failed))
}

# --- Help ---
show_help() {
    echo "Service Manager Testing Framework"
    echo "Usage: $0 [OPTION]"
    echo ""
    echo "Options:"
    echo "  --all            Run all tests (default)"
    echo "  --level <N>      Run specific level (1-5)"
    echo "  --unit           Run unit tests only (Level 2)"
    echo "  --integration    Run integration tests only (Level 3)"
    echo "  --e2e            Run E2E tests only (Level 4)"
    echo "  --fuzz           Run fuzz testing setup (Level 5)"
    echo "  --static         Run static analysis (Level 1)"
    echo "  --help           Show this help message"
}

# --- Main Entry Point ---
main() {
    local TEST_LEVEL="all"
    
    # Simple argument parsing
    if [[ "$1" == "--level" ]]; then
        TEST_LEVEL="$2"
    elif [[ "$1" != "" ]]; then
        TEST_LEVEL="${1/--/}" # remove -- prefix
    fi
    
    # Validate
    if [[ "$1" == "--help" ]]; then
        show_help
        exit 0
    fi

    echo ""
    echo "╔════════════════════════════════════════════════╗"
    echo "║   Service Manager Professional Test Suite      ║"
    echo "║   Industry-Grade Testing: Levels 1-5           ║"
    echo "╚════════════════════════════════════════════════╝"
    echo ""
    
    case "$TEST_LEVEL" in
        all)
            run_level_1_static_analysis
            run_level_2_unit_tests
            run_level_3_integration
            run_level_4_e2e
            run_level_5_fuzz
            ;;
        1|static) run_level_1_static_analysis ;;
        2|unit)   run_level_2_unit_tests ;;
        3|integration) run_level_3_integration ;;
        4|e2e)    run_level_4_e2e ;;
        5|fuzz)   run_level_5_fuzz ;;
        *)
            log_fail "Unknown option: $1"
            show_help
            exit 1
            ;;
    esac
    
    echo ""
    echo "╔════════════════════════════════════════════════╗"
    echo "║   Test Summary                                 ║"
    echo "╠════════════════════════════════════════════════╣"
    echo -e "║   Total Passed: ${GREEN}$TOTAL_PASSED${NC}                              ║"
    echo -e "║   Total Failed: ${RED}$TOTAL_FAILED${NC}                              ║"
    
    if [ $TOTAL_FAILED -eq 0 ]; then
        echo "║   Status: ${GREEN}ALL TESTS PASSED${NC} ✓                        ║"
        exit 0
    else
        echo "║   Status: ${RED}SOME TESTS FAILED${NC} ✗                        ║"
        exit 1
    fi
    echo "╚════════════════════════════════════════════════╝"
    echo ""
}

main "$@"
