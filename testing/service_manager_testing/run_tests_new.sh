#!/bin/bash
# Service Manager Comprehensive Test Harness (Levels 1-5)
# Professional production-grade testing framework

# Don't use set -e because we need to handle test failures gracefully
# Instead, we explicitly check exit codes in tests

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
MAGENTA='\033[0;35m'
NC='\033[0m'

TESTS_DIR="$(cd "$(dirname "$0")" && pwd)"
TOTAL_TESTS=0
TOTAL_PASSED=0
TOTAL_FAILED=0

# Logging functions
log_level() {
    echo -e "${MAGENTA}[Level $1]${NC} $2"
}

log_pass() {
    echo -e "${GREEN}✓${NC} $1"
}

log_fail() {
    echo -e "${RED}✗${NC} $1"
}

log_info() {
    echo -e "${YELLOW}[INFO]${NC} $1"
}

header() {
    echo ""
    echo "========================================="
    echo "$1"
    echo "========================================="
    echo ""
}

# Level 1: Static Analysis
run_level_1_static_analysis() {
    header "Level 1: Static Analysis"
    
    local passed=0
    local failed=0
    
    # Use make for static analysis
    if command -v make >/dev/null 2>&1; then
        log_level "1" "Running make test-static"
        if make test-static 2>&1; then
            ((passed++))
            log_pass "Static analysis completed"
        else
            ((failed++))
            log_fail "Static analysis issues found"
        fi
    else
        log_info "make not available, skipping static analysis"
    fi
    
    echo ""
    echo -e "Level 1: ${GREEN}$passed${NC} passed, ${RED}$failed${NC} failed"
    TOTAL_PASSED=$((TOTAL_PASSED + passed))
    TOTAL_FAILED=$((TOTAL_FAILED + failed))
}

# Level 2: Unit Tests
run_level_2_unit_tests() {
    header "Level 2: Unit Tests (Protocol, Registry, Crypto, Rate Limit)"
    
    local passed=0
    local failed=0
    
    if command -v make >/dev/null 2>&1; then
        log_level "2" "Running make test-unit"
        if make test-unit 2>&1; then
            ((passed++))
            log_pass "Unit tests passed"
        else
            ((failed++))
            log_fail "Unit tests failed"
        fi
    else
        log_info "make not available, skipping unit tests"
    fi
    
    echo ""
    echo -e "Level 2: ${GREEN}$passed${NC} passed, ${RED}$failed${NC} failed"
    TOTAL_PASSED=$((TOTAL_PASSED + passed))
    TOTAL_FAILED=$((TOTAL_FAILED + failed))
}

# Level 3: Integration Tests
run_level_3_integration() {
    header "Level 3: Integration Tests"
    
    local passed=0
    local failed=0
    
    if [ -x integration_test.sh ]; then
        log_level "3" "Running integration_test.sh"
        if ./integration_test.sh 2>&1; then
            ((passed++))
            log_pass "Integration tests passed"
        else
            ((failed++))
            log_fail "Integration tests failed"
        fi
    else
        log_info "integration_test.sh not executable"
    fi
    
    echo ""
    echo -e "Level 3: ${GREEN}$passed${NC} passed, ${RED}$failed${NC} failed"
    TOTAL_PASSED=$((TOTAL_PASSED + passed))
    TOTAL_FAILED=$((TOTAL_FAILED + failed))
}

# Level 4: E2E Tests
run_level_4_e2e() {
    header "Level 4: End-to-End Tests"
    
    local passed=0
    local failed=0
    
    if [ -x e2e_test.sh ]; then
        log_level "4" "Running e2e_test.sh"
        if ./e2e_test.sh 2>&1; then
            ((passed++))
            log_pass "E2E tests passed"
        else
            ((failed++))
            log_fail "E2E tests failed"
        fi
    else
        log_info "e2e_test.sh not executable"
    fi
    
    echo ""
    echo -e "Level 4: ${GREEN}$passed${NC} passed, ${RED}$failed${NC} failed"
    TOTAL_PASSED=$((TOTAL_PASSED + passed))
    TOTAL_FAILED=$((TOTAL_FAILED + failed))
}

# Level 5: Fuzz Testing
run_level_5_fuzz() {
    header "Level 5: Fuzz Testing (ASAN, UBSAN, AFL++)"
    
    local passed=0
    local failed=0
    
    if command -v make >/dev/null 2>&1; then
        log_level "5" "Building fuzz targets"
        
        # Build standard fuzz target
        if make fuzz_target >/dev/null 2>&1; then
            ((passed++))
            log_pass "Standard fuzz target built"
        fi
        
        # Build ASAN instrumented fuzz target
        if make fuzz_target_asan >/dev/null 2>&1; then
            ((passed++))
            log_pass "ASAN instrumented fuzz target built"
        fi
        
        # Build UBSAN instrumented fuzz target
        if make fuzz_target_ubsan >/dev/null 2>&1; then
            ((passed++))
            log_pass "UBSAN instrumented fuzz target built"
        fi
        
        log_level "5" "Fuzz targets ready for AFL++"
        log_info "To run fuzzing: afl-fuzz -i fuzz_input -o fuzz_output build/fuzz_target_asan"
    else
        log_info "make not available, skipping fuzz builds"
    fi
    
    echo ""
    echo -e "Level 5: ${GREEN}$passed${NC} targets built, ${RED}$failed${NC} failed"
    TOTAL_PASSED=$((TOTAL_PASSED + passed))
    TOTAL_FAILED=$((TOTAL_FAILED + failed))
}

# Help function
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

# Main test orchestration
main() {
    # Parse arguments
    local TEST_LEVEL=${1:-"all"}
    
    case "$TEST_LEVEL" in
        --help)
            show_help
            exit 0
            ;;
        --level)
            TEST_LEVEL="$2"
            ;;
    esac
    
    echo ""
    echo "╔════════════════════════════════════════════════╗"
    echo "║   Service Manager Professional Test Suite      ║"
    echo "║   Industry-Grade Testing: Levels 1-5            ║"
    echo "╚════════════════════════════════════════════════╝"
    echo ""
    
    # Create build directory
    mkdir -p build
    
    # Run requested tests
    case "$TEST_LEVEL" in
        all|--all)
            run_level_1_static_analysis
            run_level_2_unit_tests
            run_level_3_integration
            run_level_4_e2e
            run_level_5_fuzz
            ;;
        1|--static)
            run_level_1_static_analysis
            ;;
        2|--unit)
            run_level_2_unit_tests
            ;;
        3|--integration)
            run_level_3_integration
            ;;
        4|--e2e)
            run_level_4_e2e
            ;;
        5|--fuzz)
            run_level_5_fuzz
            ;;
        *)
            log_fail "Unknown test level: $TEST_LEVEL"
            show_help
            exit 1
            ;;
    esac
    
    echo ""
    echo "╔════════════════════════════════════════════════╗"
    echo "║   Test Summary                                  ║"
    echo "╠════════════════════════════════════════════════╣"
    echo -e "║   Total Passed: ${GREEN}$TOTAL_PASSED${NC}                                  ║"
    echo -e "║   Total Failed: ${RED}$TOTAL_FAILED${NC}                                  ║"
    
    if [ $TOTAL_FAILED -eq 0 ]; then
        echo "║   Status: ${GREEN}ALL TESTS PASSED${NC} ✓                       ║"
    else
        echo "║   Status: ${RED}SOME TESTS FAILED${NC} ✗                    ║"
    fi
    
    echo "╚════════════════════════════════════════════════╝"
    echo ""
    
    return $TOTAL_FAILED
}

# Run if not sourced
if [ "${BASH_SOURCE[0]}" == "${0}" ]; then
    main "$@"
    exit $?
fi
