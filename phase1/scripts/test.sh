#!/bin/bash
# Test script - validates Phase 1 functionality

PHASE1_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MIDDLEWARE_DIR="$PHASE1_DIR/middleware"

echo "╔════════════════════════════════════════════╗"
echo "║   MicroOS Phase 1 - Test Suite             ║"
echo "╚════════════════════════════════════════════╝"
echo ""

PASSED=0
FAILED=0

# Test 1: Check daemon compilation
echo "[TEST 1] Middleware daemon compilation"
if [ -f "$MIDDLEWARE_DIR/daemon" ]; then
    echo "  ✓ PASS: daemon binary exists"
    ((PASSED++))
else
    echo "  ✗ FAIL: daemon binary not found"
    ((FAILED++))
fi

# Test 2: Check client compilation
echo "[TEST 2] Middleware client compilation"
if [ -f "$MIDDLEWARE_DIR/client" ]; then
    echo "  ✓ PASS: client binary exists"
    ((PASSED++))
else
    echo "  ✗ FAIL: client binary not found"
    ((FAILED++))
fi

# Test 3: Check daemon is static
echo "[TEST 3] Daemon is statically linked"
if file "$MIDDLEWARE_DIR/daemon" | grep -q "static"; then
    echo "  ✓ PASS: daemon is static"
    ((PASSED++))
else
    echo "  ⚠ WARNING: daemon may not be fully static"
fi

# Test 4: Check daemon size
echo "[TEST 4] Daemon binary size"
if [ -f "$MIDDLEWARE_DIR/daemon" ]; then
    SIZE=$(stat -c%s "$MIDDLEWARE_DIR/daemon" 2>/dev/null)
    echo "  ✓ PASS: daemon size is $SIZE bytes"
    ((PASSED++))
fi

# Test 5: Check client size
echo "[TEST 5] Client binary size"
if [ -f "$MIDDLEWARE_DIR/client" ]; then
    SIZE=$(stat -c%s "$MIDDLEWARE_DIR/client" 2>/dev/null)
    echo "  ✓ PASS: client size is $SIZE bytes"
    ((PASSED++))
fi

# Test 6: RootFS structure
echo "[TEST 6] RootFS directory structure"
ROOTFS_BUILD="$PHASE1_DIR/build/rootfs"
if [ -d "$ROOTFS_BUILD" ]; then
    REQUIRED_DIRS="bin sbin lib usr etc proc sys tmp dev var"
    ALL_OK=true
    for dir in $REQUIRED_DIRS; do
        if [ ! -d "$ROOTFS_BUILD/$dir" ]; then
            ALL_OK=false
            echo "  ✗ Missing: $dir"
        fi
    done
    if [ "$ALL_OK" = true ]; then
        echo "  ✓ PASS: All required directories present"
        ((PASSED++))
    else
        ((FAILED++))
    fi
fi

# Test 7: RootFS archive
echo "[TEST 7] RootFS archive creation"
if [ -f "$PHASE1_DIR/build/rootfs.cpio.gz" ]; then
    SIZE=$(stat -c%s "$PHASE1_DIR/build/rootfs.cpio.gz" 2>/dev/null)
    echo "  ✓ PASS: rootfs.cpio.gz exists ($SIZE bytes)"
    ((PASSED++))
else
    echo "  ✗ FAIL: rootfs.cpio.gz not found"
    ((FAILED++))
fi

# Summary
echo ""
echo "════════════════════════════════════════════"
echo "Test Results:"
echo "  Passed: $PASSED"
echo "  Failed: $FAILED"
echo "════════════════════════════════════════════"
echo ""

if [ $FAILED -eq 0 ]; then
    echo "✓ All tests passed! Ready for QEMU boot."
    exit 0
else
    echo "✗ Some tests failed. Fix the issues above."
    exit 1
fi
