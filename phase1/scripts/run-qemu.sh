#!/bin/bash
# Launch QEMU with Phase 1 system

PHASE1_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
KERNEL="$PHASE1_DIR/kernel/Image"
ROOTFS="$PHASE1_DIR/build/rootfs.cpio.gz"

echo "╔═══════════════════════════════════════════════════╗"
echo "║   MicroOS Phase 1 - QEMU Boot                     ║"
echo "║   ARM64 virt machine                              ║"
echo "╚═══════════════════════════════════════════════════╝"
echo ""

# Verify dependencies
if ! command -v qemu-system-aarch64 &> /dev/null; then
    echo "ERROR: qemu-system-aarch64 not found"
    echo "Install: sudo apt-get install qemu-system-arm"
    exit 1
fi

# Check kernel
if [ ! -f "$KERNEL" ]; then
    echo "ERROR: Kernel image not found: $KERNEL"
    echo "Build kernel first: cd $PHASE1_DIR/kernel && bash build-kernel-simple.sh"
    exit 1
fi

# Check RootFS
if [ ! -f "$ROOTFS" ]; then
    echo "ERROR: RootFS not found: $ROOTFS"
    echo "Build RootFS first: cd $PHASE1_DIR/rootfs && bash build-rootfs.sh"
    exit 1
fi

echo "✓ Kernel:  $KERNEL"
echo "✓ RootFS:  $ROOTFS"
echo ""

echo "Starting QEMU ARM64 system..."
echo "  Machine:  virt"
echo "  CPU:      Cortex-A57"
echo "  Memory:   512 MB"
echo "  Cores:    2"
echo ""
echo "When booted:"
echo "  - System will show 'MicroOS System Starting...'"
echo "  - Middleware daemon will auto-start"
echo "  - Type 'exit' to poweroff"
echo ""
echo "To exit QEMU: Ctrl+A, X (release Ctrl+A, then press X)"
echo ""

# Boot QEMU
qemu-system-aarch64 \
    -machine virt \
    -cpu cortex-a57 \
    -m 512 \
    -kernel "$KERNEL" \
    -initrd "$ROOTFS" \
    -append "root=/dev/ram0 rw console=ttyAMA0 printk.time=1" \
    -serial stdio \
    -nographic \
    -smp 2 \
    -enable-kvm 2>/dev/null || \
    qemu-system-aarch64 \
        -machine virt \
        -cpu cortex-a57 \
        -m 512 \
        -kernel "$KERNEL" \
        -initrd "$ROOTFS" \
        -append "root=/dev/ram0 rw console=ttyAMA0 printk.time=1" \
        -serial stdio \
        -nographic \
        -smp 2

echo ""
echo "QEMU session ended."
