#!/usr/bin/env bash
# =============================================================================
# run_all_tests.sh — Middleware test suite master runner
#
# Phases:
#   1. Build all test binaries
#   2. Unit tests            (must pass before Phase 3)
#   3. Integration tests     (must pass before Phase 4)
#   4. Stress tests          (STRESS_DURATION seconds, default 60)
#   5. Fuzz tests            (FUZZ_ITERATIONS iterations, default 100000)
#   6. Coverage report       (lcov+genhtml or raw gcov)
#   7. Summary               (per-component PASS/FAIL + overall verdict)
#
# Environment overrides:
#   STRESS_DURATION   — seconds each stress binary runs   (default 60)
#   FUZZ_ITERATIONS   — iterations for each fuzz binary   (default 100000)
#   BUILD_DIR         — cmake build directory              (default build/)
#   SKIP_STRESS       — set to 1 to skip stress phase
#   SKIP_FUZZ         — set to 1 to skip fuzz phase
#   SKIP_COVERAGE     — set to 1 to skip coverage phase
# =============================================================================

set -euo pipefail

# ── Colours ───────────────────────────────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
CYAN='\033[0;36m'; BOLD='\033[1m'; RESET='\033[0m'
PASS="${GREEN}PASS${RESET}"; FAIL="${RED}FAIL${RESET}"

# ── Defaults ──────────────────────────────────────────────────────────
STRESS_DURATION=${STRESS_DURATION:-60}
FUZZ_ITERATIONS=${FUZZ_ITERATIONS:-100000}
BUILD_DIR=${BUILD_DIR:-"$(dirname "$0")/build"}
SKIP_STRESS=${SKIP_STRESS:-0}
SKIP_FUZZ=${SKIP_FUZZ:-0}
SKIP_COVERAGE=${SKIP_COVERAGE:-0}

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# ── Results tracking ──────────────────────────────────────────────────
declare -A RESULTS=()
OVERALL=0

log_phase() { echo -e "\n${CYAN}${BOLD}══════════════════════════════════════════════${RESET}"; \
              echo -e "${CYAN}${BOLD}  Phase $1: $2${RESET}"; \
              echo -e "${CYAN}${BOLD}══════════════════════════════════════════════${RESET}"; }

record() {      # record <label> <exit_code>
    local label=$1 rc=$2
    if [ "$rc" -eq 0 ]; then
        RESULTS["$label"]="${GREEN}PASS${RESET}"
        echo -e "  ${GREEN}✓${RESET} $label"
    else
        RESULTS["$label"]="${RED}FAIL${RESET}"
        echo -e "  ${RED}✗${RESET} $label"
        OVERALL=1
    fi
}

run_binary() {   # run_binary <path> [extra env vars…]
    local bin=$1; shift
    env "$@" "$bin" 2>&1 | sed 's/^/    /'
    return "${PIPESTATUS[0]}"
}

# ═══════════════════════════════════════════════════════════════════════
# PHASE 1 — Build
# ═══════════════════════════════════════════════════════════════════════
log_phase 1 "Build"

cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" \
      -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
      -G "Unix Makefiles" 2>&1 | tail -5

cmake --build "$BUILD_DIR" --parallel "$(nproc)" 2>&1

echo -e "${GREEN}✓ Build complete${RESET}"

# ═══════════════════════════════════════════════════════════════════════
# PHASE 2 — Unit tests
# ═══════════════════════════════════════════════════════════════════════
log_phase 2 "Unit Tests"

UNIT_BINS=(
    test_ring_buffer
    test_memory_pool
    test_io_uring_loop
    test_service_manager
    test_hal_interface
    test_audio_hal
    test_camera_hal
    test_sensor_hal
    test_gpio_hal
    test_services
)

unit_fail=0
for bin in "${UNIT_BINS[@]}"; do
    echo -e "\n  ${BOLD}» $bin${RESET}"
    rc=0
    run_binary "$BUILD_DIR/$bin" || rc=$?
    record "$bin" "$rc"
    [ "$rc" -ne 0 ] && unit_fail=1
done

if [ "$unit_fail" -ne 0 ]; then
    echo -e "\n${RED}Unit tests failed — aborting integration phase.${RESET}"
    OVERALL=1
    # Still print summary at end
    goto_summary=1
else
    goto_summary=0
fi

# ═══════════════════════════════════════════════════════════════════════
# PHASE 3 — Integration tests
# ═══════════════════════════════════════════════════════════════════════
if [ "$goto_summary" -eq 0 ]; then
log_phase 3 "Integration Tests"

INTEG_BINS=(
    test_sm_hal_integration
    test_sm_services_integration
    test_services_hal_integration
    test_memory_pool_io_uring
    test_full_system
)

integ_fail=0
for bin in "${INTEG_BINS[@]}"; do
    echo -e "\n  ${BOLD}» $bin${RESET}"
    rc=0
    run_binary "$BUILD_DIR/$bin" || rc=$?
    record "$bin" "$rc"
    [ "$rc" -ne 0 ] && integ_fail=1
done

if [ "$integ_fail" -ne 0 ]; then
    echo -e "\n${RED}Integration tests failed — aborting stress phase.${RESET}"
    OVERALL=1
    goto_summary=1
fi
fi  # unit_fail

# ═══════════════════════════════════════════════════════════════════════
# PHASE 4 — Stress tests
# ═══════════════════════════════════════════════════════════════════════
if [ "$goto_summary" -eq 0 ] && [ "$SKIP_STRESS" -ne 1 ]; then
log_phase 4 "Stress Tests  (${STRESS_DURATION}s each)"

STRESS_BINS=(
    stress_ring_buffer
    stress_memory_pool
    stress_service_manager
    stress_full_system
)

for bin in "${STRESS_BINS[@]}"; do
    echo -e "\n  ${BOLD}» $bin  [${STRESS_DURATION}s]${RESET}"
    rc=0
    run_binary "$BUILD_DIR/$bin" \
               "STRESS_DURATION=${STRESS_DURATION}" || rc=$?
    record "$bin" "$rc"
done

elif [ "$SKIP_STRESS" -eq 1 ]; then
    echo -e "\n${YELLOW}[skipped] Stress tests (SKIP_STRESS=1)${RESET}"
    for b in stress_ring_buffer stress_memory_pool stress_service_manager stress_full_system; do
        RESULTS["$b"]="${YELLOW}SKIP${RESET}"
    done
fi

# ═══════════════════════════════════════════════════════════════════════
# PHASE 5 — Fuzz tests
# ═══════════════════════════════════════════════════════════════════════
if [ "$goto_summary" -eq 0 ] && [ "$SKIP_FUZZ" -ne 1 ]; then
log_phase 5 "Fuzz Tests  (${FUZZ_ITERATIONS} iters each)"

FUZZ_BINS=(
    fuzz_sm_protocol
    fuzz_hal_input
    fuzz_ipc_messages
)

for bin in "${FUZZ_BINS[@]}"; do
    echo -e "\n  ${BOLD}» $bin${RESET}"
    rc=0
    run_binary "$BUILD_DIR/$bin" \
               "FUZZ_ITERATIONS=${FUZZ_ITERATIONS}" || rc=$?
    record "$bin" "$rc"
done

elif [ "$SKIP_FUZZ" -eq 1 ]; then
    echo -e "\n${YELLOW}[skipped] Fuzz tests (SKIP_FUZZ=1)${RESET}"
    for b in fuzz_sm_protocol fuzz_hal_input fuzz_ipc_messages; do
        RESULTS["$b"]="${YELLOW}SKIP${RESET}"
    done
fi

# ═══════════════════════════════════════════════════════════════════════
# PHASE 6 — Coverage
# ═══════════════════════════════════════════════════════════════════════
if [ "$SKIP_COVERAGE" -ne 1 ]; then
log_phase 6 "Coverage Report"

COVERAGE_DIR="$BUILD_DIR/reports/coverage"
mkdir -p "$COVERAGE_DIR"

if command -v lcov &>/dev/null && command -v genhtml &>/dev/null; then
    lcov --gcov-tool "$(command -v gcov)" \
         --capture --directory "$BUILD_DIR" \
         --output-file "$COVERAGE_DIR/coverage.info" \
         --ignore-errors source 2>/dev/null || true

    lcov --remove "$COVERAGE_DIR/coverage.info" \
         '*/framework/*' '*/mocks/*' '*/helpers/*' '/usr/*' \
         --output-file "$COVERAGE_DIR/filtered.info" 2>/dev/null || true

    genhtml "$COVERAGE_DIR/filtered.info" \
            --output-directory "$COVERAGE_DIR/html" 2>/dev/null || true

    echo -e "${GREEN}✓ Coverage HTML: $COVERAGE_DIR/html/index.html${RESET}"

elif command -v gcov &>/dev/null; then
    echo -e "${YELLOW}lcov/genhtml not found — generating raw .gcov files${RESET}"
    (
        cd "$BUILD_DIR"
        find . -name "*.gcda" -exec gcov {} \; 2>/dev/null | \
            grep -E "Lines executed|File" | head -60 || true
    )
    echo -e "${YELLOW}Install lcov for HTML report: sudo apt install lcov${RESET}"
else
    echo -e "${YELLOW}gcov not found — skipping coverage${RESET}"
fi
else
    echo -e "\n${YELLOW}[skipped] Coverage (SKIP_COVERAGE=1)${RESET}"
fi

# ═══════════════════════════════════════════════════════════════════════
# PHASE 7 — Summary
# ═══════════════════════════════════════════════════════════════════════
log_phase 7 "Summary"

printf "  %-40s %s\n" "Component" "Result"
printf "  %-40s %s\n" "$(printf '%0.s-' {1..40})" "------"

for key in "${!RESULTS[@]}"; do
    printf "  %-40s " "$key"
    echo -e "${RESULTS[$key]}"
done | sort

echo ""
if [ "$OVERALL" -eq 0 ]; then
    echo -e "${GREEN}${BOLD}  ✓ Overall verdict: PASS${RESET}"
else
    echo -e "${RED}${BOLD}  ✗ Overall verdict: FAIL${RESET}"
fi

exit "$OVERALL"
