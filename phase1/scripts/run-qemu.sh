#!/bin/bash
# Launch QEMU with Phase 1 system
# Ye script QEMU (virtual machine) ko start karta hai aur MicroOS ko run karta hai

# PHASE1_DIR: Current project ka main folder (jahan se ye script run ho raha hai us se ek level upar)
# "${BASH_SOURCE[0]}" = is script ka name/path
# dirname = directory ka name nikalo (script ke folder tak)
# /.." = ek level upar jao (scripts folder se phase1 folder tak)
PHASE1_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# KERNEL: Linux kernel image ka path (jo compiled kernel image hai ARM64 ke liye)
# Image = Linux kernel binary jis ko QEMU boot karega
KERNEL="$PHASE1_DIR/kernel/Image"

# ROOTFS: Compressed root filesystem ka path (ye Linux ka complete filesystem hai - gzip se compress)
# rootfs.cpio.gz = complete filesystem jo BusyBox aur middleware dono ke saath hai
ROOTFS="$PHASE1_DIR/build/rootfs.cpio.gz"

# Beautiful banner print karo taakay user ko pata chale kaunsa system start ho raha hai
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
