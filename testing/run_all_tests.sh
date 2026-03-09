#!/usr/bin/env bash
# run_all_tests.sh — 6-phase test runner (spec Part 11)
# Phase 1 – Build
# Phase 2 – Unit tests
# Phase 3 – Integration tests
# Phase 4 – Fuzz (standalone, 1 000 000 iterations)
# Phase 5 – Stress tests (short soak)
# Phase 6 – Summary (tests/passed/failed/skipped/coverage/peak_mem/sanitizer_errors)

set -euo pipefail
cd "$(dirname "$0")"

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
CYAN='\033[0;36m'; BOLD='\033[1m'; RESET='\033[0m'

TOTAL=0; PASSED=0; FAILED=0; SKIPPED=0; SANITIZER_ERRORS=0
BUILD_DIR="${BUILD_DIR:-build}"
LOG_DIR="${LOG_DIR:-logs}"
mkdir -p "$LOG_DIR"

phase() {
  echo -e "\n${CYAN}${BOLD}══════════════════════════════════════════${RESET}"
  echo -e "${CYAN}${BOLD}  PHASE $1 – $2${RESET}"
  echo -e "${CYAN}${BOLD}══════════════════════════════════════════${RESET}"
}

run_test() {
  local bin="$1"; shift
  local label="${1:-$(basename "$bin")}"; shift || true
  local logfile="$LOG_DIR/$(basename "$bin").log"
  TOTAL=$((TOTAL+1))
  echo -e "  ${YELLOW}RUN${RESET}  $label"
  if "$bin" --verbose 2>&1 | tee "$logfile"; then
    local p f skip san
    p=$(grep -c ' PASS' "$logfile" 2>/dev/null || true)
    f=$(grep -c ' FAIL' "$logfile" 2>/dev/null || true)
    skip=$(grep -c 'IGNORE' "$logfile" 2>/dev/null || true)
    san=$(grep -cE 'runtime error:|ERROR:' "$logfile" 2>/dev/null || true)
    PASSED=$((PASSED+p)); FAILED=$((FAILED+f)); SKIPPED=$((SKIPPED+skip))
    SANITIZER_ERRORS=$((SANITIZER_ERRORS+san))
    if [ "$f" -gt 0 ] || [ "$san" -gt 0 ]; then
      echo -e "  ${RED}FAILED${RESET} $label  (fail=$f asan=$san)"
    else
      echo -e "  ${GREEN}OK${RESET}     $label  (pass=$p skip=$skip)"
    fi
  else
    echo -e "  ${RED}CRASH${RESET}  $label (binary returned non-zero)"
    FAILED=$((FAILED+1))
  fi
}

# ── PHASE 1: BUILD ──────────────────────────────────────────────────────────
phase 1 "BUILD"
cmake -S . -B "$BUILD_DIR" \
      -DCMAKE_BUILD_TYPE=Debug 2>&1 | tail -5

cmake --build "$BUILD_DIR" --parallel "$(nproc)" 2>&1 | tail -20
echo -e "${GREEN}Build complete.${RESET}"

# ── PHASE 2: UNIT TESTS ─────────────────────────────────────────────────────
phase 2 "UNIT TESTS"
for bin in "$BUILD_DIR"/unit/test_*; do
  [ -x "$bin" ] || continue
  run_test "$bin" "$(basename "$bin")"
done

# ── PHASE 3: INTEGRATION TESTS ──────────────────────────────────────────────
phase 3 "INTEGRATION TESTS"
for bin in "$BUILD_DIR"/integration/test_*; do
  [ -x "$bin" ] || continue
  run_test "$bin" "$(basename "$bin")"
done

# ── PHASE 4: FUZZ TESTS ─────────────────────────────────────────────────────
phase 4 "FUZZ TESTS (standalone 1 000 000 iterations)"
export FUZZ_ITERATIONS=${FUZZ_ITERATIONS:-1000000}
for bin in "$BUILD_DIR"/fuzz/fuzz_*; do
  [ -x "$bin" ] || continue
  label="$(basename "$bin")"
  echo -e "  ${YELLOW}RUN${RESET}  $label  (${FUZZ_ITERATIONS} iterations)"
  logfile="$LOG_DIR/$label.log"
  TOTAL=$((TOTAL+1))
  if "$bin" 2>&1 | tee "$logfile" | grep -q "PASSED"; then
    echo -e "  ${GREEN}OK${RESET}     $label"
    PASSED=$((PASSED+1))
  else
    echo -e "  ${RED}FAILED${RESET} $label"
    FAILED=$((FAILED+1))
  fi
done

# ── PHASE 5: STRESS TESTS ───────────────────────────────────────────────────
phase 5 "STRESS TESTS (${STRESS_DURATION:-5}s soak)"
export STRESS_DURATION=${STRESS_DURATION:-5}
for bin in "$BUILD_DIR"/stress/stress_*; do
  [ -x "$bin" ] || continue
  run_test "$bin" "$(basename "$bin")"
done

# ── PHASE 6: SUMMARY ────────────────────────────────────────────────────────
phase 6 "SUMMARY"

COVERAGE_PCT="N/A"
if command -v gcovr &>/dev/null; then
  COVERAGE_PCT=$(gcovr --root .. --filter '.*dev/core.*' \
                       --print-summary 2>/dev/null \
                 | grep -oP '\d+\.\d+(?=%)' | head -1 || echo "N/A")
fi

PEAK_MEM="N/A"
if command -v /usr/bin/time &>/dev/null; then
  PEAK_MEM="(rerun with '/usr/bin/time -v' for details)"
fi

echo ""
printf "  %-18s %s\n" "Tests run   :" "$TOTAL"
printf "  %-18s %s\n" "Passed      :" "$PASSED"
printf "  %-18s %s\n" "Failed      :" "$FAILED"
printf "  %-18s %s\n" "Skipped     :" "$SKIPPED"
printf "  %-18s %s %%\n" "Coverage    :" "$COVERAGE_PCT"
printf "  %-18s %s\n" "Peak memory :" "$PEAK_MEM"
printf "  %-18s %s error(s)\n" "Sanitizer   :" "$SANITIZER_ERRORS"
echo ""

if [ "$FAILED" -gt 0 ] || [ "$SANITIZER_ERRORS" -gt 0 ]; then
  echo -e "${RED}${BOLD}RESULT: FAIL  ($FAILED failures, $SANITIZER_ERRORS sanitizer errors)${RESET}"
  exit 1
else
  echo -e "${GREEN}${BOLD}RESULT: PASS  (all $PASSED assertions green)${RESET}"
  exit 0
fi
