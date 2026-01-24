#!/bin/bash
# Boot Phase 3 with QEMU's built-in ARM64 kernel
# No kernel compilation needed!

QEMU_CMD="qemu-system-aarch64"

if ! command -v $QEMU_CMD &>/dev/null; then
    echo "❌ QEMU not found. Install: apt-get install qemu-system-arm"
    exit 1
fi

echo "Phase 3 Mobile GUI - QEMU Boot"
echo "==============================="
echo ""

# Check rootfs
if [ ! -f "rootfs_minimal.cpio" ]; then
    echo "⚠️  Creating minimal rootfs..."
    bash simple-build.sh
fi

echo "✅ Using QEMU internal ARM64 kernel"
echo "✅ RootFS: rootfs_minimal.cpio"
echo ""
echo "Booting..."
echo ""

# Boot with QEMU's internal ARM64 kernel
$QEMU_CMD \
    -M virt \
    -cpu cortex-a57 \
    -m 2048 \
    -smp 4 \
    -initrd rootfs_minimal.cpio \
    -append "root=/dev/ram rw console=ttyAMA0" \
    -display gtk \
    -device virtio-tablet \
    -device virtio-keyboard \
    -serial stdio

