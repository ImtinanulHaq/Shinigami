#!/bin/bash
# Simplified RootFS Creation for ARM64

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PHASE1_DIR="$(dirname "${SCRIPT_DIR}")"
ROOTFS_DIR="${SCRIPT_DIR}/../build/rootfs"
BUILD_DIR="${SCRIPT_DIR}/../build"
MIDDLEWARE_DIR="${PHASE1_DIR}/middleware"

echo "╔═══════════════════════════════════════════════════╗"
echo "║   MicroOS RootFS Builder for ARM64                ║"
echo "╚═══════════════════════════════════════════════════╝"
echo ""

# Check if middleware binaries exist
if [ ! -f "$MIDDLEWARE_DIR/daemon" ]; then
    echo "ERROR: Middleware daemon not found at $MIDDLEWARE_DIR/daemon"
    exit 1
fi

if [ ! -f "$MIDDLEWARE_DIR/client" ]; then
    echo "ERROR: Middleware client not found at $MIDDLEWARE_DIR/client"
    exit 1
fi

echo "[1/4] Creating RootFS directory structure..."
rm -rf "$ROOTFS_DIR"
mkdir -p "$ROOTFS_DIR"/{bin,sbin,lib,usr/{bin,sbin,lib},etc/{init.d,rc.d},proc,sys,tmp,dev,var/{log,run}}
chmod 1777 "$ROOTFS_DIR/tmp"
echo "  ✓ Directory structure created"
echo ""

echo "[2/4] Creating essential files..."

# Device nodes
mknod -m 622 "$ROOTFS_DIR/dev/console" c 5 1 2>/dev/null || true
mknod -m 666 "$ROOTFS_DIR/dev/null" c 1 3 2>/dev/null || true
mknod -m 666 "$ROOTFS_DIR/dev/zero" c 1 5 2>/dev/null || true
mknod -m 666 "$ROOTFS_DIR/dev/full" c 1 7 2>/dev/null || true
mknod -m 644 "$ROOTFS_DIR/dev/random" c 1 8 2>/dev/null || true
mknod -m 644 "$ROOTFS_DIR/dev/urandom" c 1 9 2>/dev/null || true
mknod -m 666 "$ROOTFS_DIR/dev/tty" c 5 0 2>/dev/null || true
mknod -m 666 "$ROOTFS_DIR/dev/ptmx" c 5 2 2>/dev/null || true

# Symlinks
ln -sf /usr/bin "$ROOTFS_DIR/bin" 2>/dev/null || true
ln -sf /usr/sbin "$ROOTFS_DIR/sbin" 2>/dev/null || true
ln -sf /usr/lib "$ROOTFS_DIR/lib" 2>/dev/null || true
ln -sf /usr/lib "$ROOTFS_DIR/lib64" 2>/dev/null || true
ln -sf /proc/self/fd "$ROOTFS_DIR/dev/fd" 2>/dev/null || true

# Essential files
cat > "$ROOTFS_DIR/etc/passwd" << 'EOF'
root:x:0:0:root:/root:/bin/sh
daemon:x:1:1:daemon:/usr/sbin:/usr/sbin/nologin
bin:x:2:2:bin:/bin:/usr/sbin/nologin
sys:x:3:3:sys:/dev:/usr/sbin/nologin
nobody:x:65534:65534:nobody:/nonexistent:/usr/sbin/nologin
EOF

cat > "$ROOTFS_DIR/etc/group" << 'EOF'
root:x:0:
daemon:x:1:
bin:x:2:
sys:x:3:
nogroup:x:65534:
EOF

cat > "$ROOTFS_DIR/etc/hostname" << 'EOF'
MicroOS
EOF

cat > "$ROOTFS_DIR/etc/inittab" << 'EOF'
::sysinit:/etc/rc.sysinit
::respawn:/bin/sh
::ctrlaltdel:/bin/poweroff
::shutdown:/bin/umount -a -r
EOF

cat > "$ROOTFS_DIR/etc/rc.sysinit" << 'EOF'
#!/bin/sh
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev
mkdir -p /dev/pts
mount -t devpts devpts /dev/pts
hostname MicroOS
echo "MicroOS System Starting..."
echo "Starting middleware daemon..."
/usr/sbin/middleware-daemon &
echo "System ready. Type 'exit' to poweroff."
EOF

chmod +x "$ROOTFS_DIR/etc/rc.sysinit"

# Init wrapper
cat > "$ROOTFS_DIR/init" << 'EOF'
#!/bin/sh
exec /sbin/init
EOF
chmod +x "$ROOTFS_DIR/init"

echo "  ✓ Essential files created"
echo ""

echo "[3/4] Installing middleware components..."
# Copy middleware binaries
cp "$MIDDLEWARE_DIR/daemon" "$ROOTFS_DIR/usr/sbin/middleware-daemon"
cp "$MIDDLEWARE_DIR/client" "$ROOTFS_DIR/usr/bin/middleware-client"
chmod +x "$ROOTFS_DIR/usr/sbin/middleware-daemon"
chmod +x "$ROOTFS_DIR/usr/bin/middleware-client"
echo "  ✓ Middleware daemon installed"
echo "  ✓ Middleware client installed"
echo ""

echo "[4/4] Creating compressed RootFS archive..."
cd "$ROOTFS_DIR"
find . | cpio -o -H newc 2>/dev/null | gzip -9 > "$BUILD_DIR/rootfs.cpio.gz"

SIZE=$(stat -c%s "$BUILD_DIR/rootfs.cpio.gz")
echo "  ✓ RootFS archive created"
echo ""

echo "╔═══════════════════════════════════════════════════╗"
echo "║  ✅ ROOTFS BUILD COMPLETE                        ║"
echo "╚═══════════════════════════════════════════════════╝"
echo ""
echo "RootFS: $BUILD_DIR/rootfs.cpio.gz ($SIZE bytes)"
