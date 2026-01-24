#!/bin/bash
# RootFS Creation Script for ARM64
# Builds a minimal BusyBox-based root filesystem

# Enable strict error checking - script stops if any command fails
set -e

# SCRIPT_DIR: Get absolute path of this script's directory (jis folder mein ye script hai us folder ka path)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# ROOTFS_DIR: Define where final root filesystem will be built (root filesystem banane ka location)
ROOTFS_DIR="${SCRIPT_DIR}/../build/rootfs"

# BUSYBOX_VERSION: Specify which BusyBox version to download (BusyBox ka version - ye Linux ka minimal version hai)
BUSYBOX_VERSION="1.36.1"

# BUSYBOX_DIR: Directory where BusyBox source code will be downloaded (BusyBox ki files yahan par aayengi)
BUSYBOX_DIR="${SCRIPT_DIR}/../busybox"

# OVERLAY_DIR: Directory containing custom configuration files to add to RootFS (custom files ke liye folder)
OVERLAY_DIR="${SCRIPT_DIR}/../overlay"

# ARCH: CPU architecture type - arm64 means 64-bit ARM (64-bit ARM wale processor ke liye)
ARCH="arm64"

# CROSS_COMPILE: ARM64 compiler prefix used for cross-compilation (ARM64 ke liye compiler ka naam - aarch64 wala)
CROSS_COMPILE="aarch64-linux-gnu-"

# Print welcome message with formatting (script start hone ka banner dikhao)
echo "======================================"
echo "MicroOS RootFS Build Script"
echo "======================================"

# Step 1/6: Create all necessary directory structure (initial folders banao)
echo "[1/6] Creating directory structure..."

# mkdir -p: Create directories (recursively) with these paths (sab folders banao)
# $ROOTFS_DIR/{bin,sbin,lib,usr/{bin,sbin,lib},etc,proc,sys,tmp,dev,var/{log,run}}
# yani: bin, sbin, lib, usr/bin, usr/sbin, usr/lib, etc, proc, sys, tmp, dev, var/log, var/run
mkdir -p $ROOTFS_DIR/{bin,sbin,lib,usr/{bin,sbin,lib},etc,proc,sys,tmp,dev,var/{log,run}}

# chmod 1777: Set /tmp directory permissions (1777 = sticky bit + read/write for everyone)
# /tmp sab ke liye readable aur writable ho taakay temporary files banaye ja saken
chmod 1777 $ROOTFS_DIR/tmp

# Create essential device nodes
echo "[2/6] Creating device nodes..."
mknod -m 622 $ROOTFS_DIR/dev/console c 5 1
mknod -m 666 $ROOTFS_DIR/dev/null c 1 3
mknod -m 666 $ROOTFS_DIR/dev/zero c 1 5
mknod -m 666 $ROOTFS_DIR/dev/full c 1 7
mknod -m 644 $ROOTFS_DIR/dev/random c 1 8
mknod -m 644 $ROOTFS_DIR/dev/urandom c 1 9
mknod -m 666 $ROOTFS_DIR/dev/tty c 5 0
mknod -m 666 $ROOTFS_DIR/dev/ptmx c 5 2

# Create symlinks
ln -sf /usr/bin $ROOTFS_DIR/bin
ln -sf /usr/sbin $ROOTFS_DIR/sbin
ln -sf /usr/lib $ROOTFS_DIR/lib
ln -sf /usr/lib $ROOTFS_DIR/lib64
ln -sf /proc/self/fd $ROOTFS_DIR/dev/fd
ln -sf /proc/self/fd/0 $ROOTFS_DIR/dev/stdin
ln -sf /proc/self/fd/1 $ROOTFS_DIR/dev/stdout
ln -sf /proc/self/fd/2 $ROOTFS_DIR/dev/stderr

echo "[2.5/6] Downloading and building BusyBox..."
if [ ! -d "$BUSYBOX_DIR" ]; then
    mkdir -p "$BUSYBOX_DIR"
    cd "$BUSYBOX_DIR"
    wget -q "https://busybox.net/downloads/busybox-${BUSYBOX_VERSION}.tar.bz2" -O busybox.tar.bz2
    tar -xf busybox.tar.bz2
    cd "busybox-${BUSYBOX_VERSION}"
else
    cd "$BUSYBOX_DIR/busybox-${BUSYBOX_VERSION}"
fi

# Build BusyBox
make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE -j$(nproc) > /dev/null 2>&1
make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE CONFIG_PREFIX=$ROOTFS_DIR install > /dev/null 2>&1
echo "  ✓ BusyBox built and installed"

# Copy overlay files
echo "[3/6] Copying overlay files..."
if [ -d "$OVERLAY_DIR" ]; then
    cp -r "$OVERLAY_DIR"/* "$ROOTFS_DIR/" 2>/dev/null || true
fi

# Create essential files
echo "[4/6] Creating configuration files..."

# passwd
cat > $ROOTFS_DIR/etc/passwd << 'EOF'
root:x:0:0:root:/root:/bin/sh
daemon:x:1:1:daemon:/usr/sbin:/usr/sbin/nologin
bin:x:2:2:bin:/bin:/usr/sbin/nologin
sys:x:3:3:sys:/dev:/usr/sbin/nologin
nobody:x:65534:65534:nobody:/nonexistent:/usr/sbin/nologin
EOF

# group
cat > $ROOTFS_DIR/etc/group << 'EOF'
root:x:0:
daemon:x:1:
bin:x:2:
sys:x:3:
nogroup:x:65534:
EOF

# inittab
cat > $ROOTFS_DIR/etc/inittab << 'EOF'
::sysinit:/etc/rc.sysinit
::respawn:/bin/getty 115200 console
::ctrlaltdel:/bin/poweroff
::shutdown:/bin/umount -a -r
EOF

# rc.sysinit
cat > $ROOTFS_DIR/etc/rc.sysinit << 'EOF'
#!/bin/sh
# System initialization script

echo "========================================"
echo "MicroOS System Starting..."
echo "========================================"

# Mount essential filesystems
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev

# Create /dev/pts
mkdir -p /dev/pts
mount -t devpts devpts /dev/pts

# Set hostname
hostname MicroOS

# Display startup message
echo ""
echo "✓ System initialization complete"
echo "✓ Starting middleware daemon..."

# Start middleware daemon
/usr/sbin/middleware-daemon &
DAEMON_PID=$!

echo "✓ Middleware daemon started (PID: $DAEMON_PID)"
echo ""
EOF

chmod +x $ROOTFS_DIR/etc/rc.sysinit

# Copy middleware daemon to RootFS
echo "[5/6] Installing middleware components..."
if [ -f "${SCRIPT_DIR}/../daemon" ]; then
    cp "${SCRIPT_DIR}/../daemon" "$ROOTFS_DIR/usr/sbin/middleware-daemon"
    chmod +x "$ROOTFS_DIR/usr/sbin/middleware-daemon"
    echo "  ✓ Middleware daemon installed"
fi

if [ -f "${SCRIPT_DIR}/../client" ]; then
    cp "${SCRIPT_DIR}/../client" "$ROOTFS_DIR/usr/bin/middleware-client"
    chmod +x "$ROOTFS_DIR/usr/bin/middleware-client"
    echo "  ✓ Middleware client installed"
fi

# Create compressed archive
echo "[6/6] Creating RootFS archive..."
cd $ROOTFS_DIR
find . -print0 | cpio --null -ov --format=newc 2>/dev/null | gzip -9 > ../rootfs.cpio.gz
echo "  ✓ RootFS created: ${ROOTFS_DIR}/../rootfs.cpio.gz"

echo ""
echo "======================================"
echo "✓ RootFS build complete!"
echo "======================================"
echo ""
echo "RootFS location: $ROOTFS_DIR"
echo "Compressed: ${ROOTFS_DIR}/../rootfs.cpio.gz"
