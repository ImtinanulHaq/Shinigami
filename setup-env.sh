#!/bin/bash
# Development environment setup script
# Checks for all required tools and guides user through installation

set -e

echo "╔════════════════════════════════════════════╗"
echo "║   MicroOS Development Environment Setup    ║"
echo "╚════════════════════════════════════════════╝"
echo ""

MISSING_TOOLS=()
OPTIONAL_TOOLS=()

# Check required tools
check_required_tool() {
    local tool=$1
    local package=$2
    
    if ! command -v $tool &> /dev/null; then
        MISSING_TOOLS+=("$package")
        echo "✗ $tool (missing)"
    else
        echo "✓ $tool"
    fi
}

# Check optional tools
check_optional_tool() {
    local tool=$1
    local package=$2
    
    if ! command -v $tool &> /dev/null; then
        OPTIONAL_TOOLS+=("$package")
        echo "⚠ $tool (optional)"
    else
        echo "✓ $tool"
    fi
}

echo "Checking required tools..."
check_required_tool "gcc" "build-essential"
check_required_tool "make" "build-essential"
check_required_tool "wget" "wget"
check_required_tool "aarch64-linux-gnu-gcc" "gcc-aarch64-linux-gnu"
check_required_tool "qemu-system-aarch64" "qemu-system-arm"

echo ""
echo "Checking optional tools..."
check_optional_tool "gdb-multiarch" "gdb-multiarch"
check_optional_tool "strace" "strace"

echo ""
echo "════════════════════════════════════════════"

if [ ${#MISSING_TOOLS[@]} -eq 0 ]; then
    echo "✓ All required tools are installed!"
    echo ""
    echo "Your system is ready for MicroOS development."
    echo ""
    echo "Get started with:"
    echo "  cd /home/muhammad-imtinan-ul-haq/Desktop/middleware"
    echo "  make help"
else
    echo "✗ Missing required tools:"
    for tool in "${MISSING_TOOLS[@]}"; do
        echo "  - $tool"
    done
    echo ""
    echo "Install with:"
    echo "  sudo apt-get install ${MISSING_TOOLS[@]}"
    exit 1
fi

if [ ${#OPTIONAL_TOOLS[@]} -gt 0 ]; then
    echo ""
    echo "⚠ Optional tools missing (recommended):"
    for tool in "${OPTIONAL_TOOLS[@]}"; do
        echo "  - $tool"
    done
    echo ""
    echo "Install with:"
    echo "  sudo apt-get install ${OPTIONAL_TOOLS[@]}"
fi

echo ""
echo "════════════════════════════════════════════"
echo "Setup complete! Happy coding! 🚀"
