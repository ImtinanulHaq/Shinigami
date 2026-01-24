#!/bin/bash
# Build Linux Kernel for ARM64 QEMU

set -e

KERNEL_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$KERNEL_DIR/build"
CONFIG_FILE="$KERNEL_DIR/arm64.config"

echo "╔═══════════════════════════════════════════════════╗"
echo "║   Linux Kernel Build for ARM64 QEMU               ║"
echo "╚═══════════════════════════════════════════════════╝"
echo ""

# Check cross-compiler
if ! command -v aarch64-linux-gnu-gcc &> /dev/null; then
    echo "ERROR: aarch64-linux-gnu-gcc not found"
    echo "Install: sudo apt-get install gcc-aarch64-linux-gnu"
    exit 1
fi

echo "[1/5] Cross-compiler check: ✓"
echo "  $(aarch64-linux-gnu-gcc --version | head -1)"
echo ""

# Check if kernel source exists
cd "$BUILD_DIR"
if [ ! -d "linux-6.8" ]; then
    echo "[2/5] Extracting kernel source..."
    if [ ! -f "linux.tar.xz" ]; then
        echo "  ERROR: linux.tar.xz not found"
        exit 1
    fi
    tar -xf linux.tar.xz
    echo "  ✓ Extracted"
else
    echo "[2/5] Kernel source already extracted"
fi

cd linux-6.8

echo ""
echo "[3/5] Configuring kernel..."

# Create minimal kernel config
cat > .config << 'KCONFIG'
CONFIG_ARM64=y
CONFIG_ARCH_FLATMEM_ENABLE=y
CONFIG_FLATMEM=y
CONFIG_64BIT=y
CONFIG_QEMU_FIRMWARE=y
CONFIG_PRINTK=y
CONFIG_EARLY_PRINTK=y
CONFIG_VT=y
CONFIG_VT_CONSOLE=y
CONFIG_SERIAL_AMBA_PL011=y
CONFIG_SERIAL_AMBA_PL011_CONSOLE=y
CONFIG_DEVTMPFS=y
CONFIG_DEVTMPFS_MOUNT=y
CONFIG_TMPFS=y
CONFIG_PROC_FS=y
CONFIG_SYSFS=y
CONFIG_POSIX_TIMERS=y
CONFIG_BLK_DEV_INITRD=y
CONFIG_BLK_DEV_RAM=y
CONFIG_BLK_DEV_RAM_SIZE=262144
CONFIG_MODULES=y
CONFIG_MODULE_UNLOAD=y
CONFIG_NET=y
CONFIG_INET=y
CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS=y
CONFIG_MMU=y
CONFIG_ZONE_DMA=y
CONFIG_ZONE_DMA32=y
CONFIG_HAVE_MEMORY_PRESENT=y
CONFIG_HAVE_SETUP_PER_CPU_AREA=y
CONFIG_NEED_PER_CPU_EMBED_FIRST_CHUNK=y
CONFIG_HAVE_KPROBES=y
CONFIG_HAVE_KRETPROBES=y
CONFIG_HAVE_NMI=y
CONFIG_HZ_FIXED=0
CONFIG_HZ=250
CONFIG_SCHED_HRTICK=y
CONFIG_GENERIC_SCHED_CLOCK=y
CONFIG_TICK_ONESHOT=y
CONFIG_NO_HZ=y
CONFIG_NO_HZ_IDLE=y
CONFIG_HIGH_RES_TIMERS=y
CONFIG_GENERIC_CLOCKEVENTS=y
CONFIG_GENERIC_CLOCKEVENTS_BROADCAST=y
KCONFIG

# Make oldconfig to apply defaults for missing options
echo "  Generating default config..."
ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- make oldconfig < /dev/null > /dev/null 2>&1 || true

echo "  ✓ Configured"
echo ""

echo "[4/5] Compiling kernel (this may take 10-15 minutes)..."
ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- make -j$(nproc) Image 2>&1 | tail -20

if [ -f "arch/arm64/boot/Image" ]; then
    echo ""
    echo "[5/5] Installing kernel..."
    cp arch/arm64/boot/Image "$KERNEL_DIR/Image"
    SIZE=$(stat -c%s "$KERNEL_DIR/Image")
    echo "  ✓ Kernel installed: $KERNEL_DIR/Image ($SIZE bytes)"
    echo ""
    echo "╔═══════════════════════════════════════════════════╗"
    echo "║  ✅ KERNEL BUILD COMPLETE                        ║"
    echo "╚═══════════════════════════════════════════════════╝"
else
    echo "ERROR: Kernel build failed"
    exit 1
fi
