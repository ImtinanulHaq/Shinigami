#!/bin/bash
# Master build script for Phase 1

set -e

PHASE1_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MIDDLEWARE_DIR="$PHASE1_DIR/middleware"
ROOTFS_DIR="$PHASE1_DIR/rootfs"
BUILD_DIR="$PHASE1_DIR/build"
KERNEL_DIR="$PHASE1_DIR/kernel"

echo "╔════════════════════════════════════════════╗"
echo "║   MicroOS Phase 1 - Build System           ║"
echo "║   Building for ARM64 (aarch64-linux-gnu)   ║"
echo "╚════════════════════════════════════════════╝"
echo ""

# Check for cross-compiler
echo "[1/4] Checking for cross-compiler..."
if ! command -v aarch64-linux-gnu-gcc &> /dev/null; then
    echo "✗ aarch64-linux-gnu-gcc not found"
    echo "  Install: sudo apt-get install gcc-aarch64-linux-gnu"
    exit 1
fi
echo "✓ Cross-compiler found"

# Build middleware daemon and client
echo ""
echo "[2/4] Building middleware components..."
cd $MIDDLEWARE_DIR
make clean > /dev/null 2>&1 || true
make all
echo "✓ Middleware components built"

# Build RootFS
echo ""
echo "[3/4] Building RootFS..."
bash $ROOTFS_DIR/scripts/mkrootfs.sh
echo "✓ RootFS built"

# Summary
echo ""
echo "[4/4] Build Summary"
echo "======================================"

if [ -f "$MIDDLEWARE_DIR/daemon" ]; then
    SIZE=$(stat -f%z "$MIDDLEWARE_DIR/daemon" 2>/dev/null || stat -c%s "$MIDDLEWARE_DIR/daemon" 2>/dev/null)
    echo "✓ Middleware Daemon:  $MIDDLEWARE_DIR/daemon ($SIZE bytes)"
fi

if [ -f "$MIDDLEWARE_DIR/client" ]; then
    SIZE=$(stat -f%z "$MIDDLEWARE_DIR/client" 2>/dev/null || stat -c%s "$MIDDLEWARE_DIR/client" 2>/dev/null)
    echo "✓ Middleware Client:  $MIDDLEWARE_DIR/client ($SIZE bytes)"
fi

if [ -f "$BUILD_DIR/rootfs.cpio.gz" ]; then
    SIZE=$(stat -f%z "$BUILD_DIR/rootfs.cpio.gz" 2>/dev/null || stat -c%s "$BUILD_DIR/rootfs.cpio.gz" 2>/dev/null)
    echo "✓ RootFS Archive:     $BUILD_DIR/rootfs.cpio.gz ($SIZE bytes)"
fi

echo ""
echo "======================================"
echo "✓ Build complete!"
echo ""
echo "Next steps:"
echo "  1. Run QEMU: ./scripts/run-qemu.sh"
echo "  2. Connect: ./middleware/client /var/run/middleware.sock"
echo ""
