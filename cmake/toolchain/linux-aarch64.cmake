# =============================================================================
# cmake/toolchains/linux-aarch64.cmake
# Cross-compilation toolchain for AArch64 (ARM64) Linux targets.
#
# Prerequisites:
#   apt-get install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu
#   binutils-aarch64-linux-gnu
#
# Usage:
#   cmake --preset cross-aarch64
#   or: cmake -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/linux-aarch64.cmake ..
# =============================================================================

set(CMAKE_SYSTEM_NAME    Linux)
set(CMAKE_SYSTEM_VERSION 1)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# ---------------------------------------------------------------------------
# Compiler selection
# Allow override via environment: CROSS_COMPILE=aarch64-linux-gnu- cmake ...
# ---------------------------------------------------------------------------
set(_cross_prefix "aarch64-linux-gnu")
if(DEFINED ENV{CROSS_COMPILE})
    string(REGEX REPLACE "-$" "" _cross_prefix "$ENV{CROSS_COMPILE}")
endif()

find_program(CMAKE_C_COMPILER   NAMES "${_cross_prefix}-gcc"   REQUIRED)
find_program(CMAKE_CXX_COMPILER NAMES "${_cross_prefix}-g++"   REQUIRED)
find_program(CMAKE_ASM_COMPILER NAMES "${_cross_prefix}-gcc"   REQUIRED)
find_program(CMAKE_AR           NAMES "${_cross_prefix}-ar"    REQUIRED)
find_program(CMAKE_STRIP        NAMES "${_cross_prefix}-strip"  REQUIRED)
find_program(CMAKE_OBJCOPY      NAMES "${_cross_prefix}-objcopy" REQUIRED)
find_program(CMAKE_RANLIB       NAMES "${_cross_prefix}-ranlib" REQUIRED)

# ---------------------------------------------------------------------------
# Sysroot — point to the target system's headers/libraries
# Set AARCH64_SYSROOT env var or -DCMAKE_SYSROOT=/path/to/sysroot
# ---------------------------------------------------------------------------
if(DEFINED ENV{AARCH64_SYSROOT})
    set(CMAKE_SYSROOT "$ENV{AARCH64_SYSROOT}")
endif()

if(CMAKE_SYSROOT)
    set(CMAKE_FIND_ROOT_PATH "${CMAKE_SYSROOT}")
    message(STATUS "[MW] AArch64 sysroot: ${CMAKE_SYSROOT}")
else()
    # Default Debian/Ubuntu multiarch sysroot
    set(CMAKE_FIND_ROOT_PATH "/usr/${_cross_prefix}")
    message(STATUS "[MW] AArch64 find root: ${CMAKE_FIND_ROOT_PATH}")
endif()

# ---------------------------------------------------------------------------
# Search behaviour — find libs/includes in sysroot, tools on host
# ---------------------------------------------------------------------------
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)   # tools run on host
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)    # libs from target sysroot
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)    # headers from target sysroot
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)    # cmake packages from sysroot

# ---------------------------------------------------------------------------
# Architecture-specific flags
# -mcpu=generic handles the widest range of AArch64 hardware
# -moutline-atomics: faster atomics on heterogeneous big.LITTLE SoCs
# ---------------------------------------------------------------------------
string(APPEND CMAKE_C_FLAGS_INIT
    " -mcpu=generic "
    " -moutline-atomics "
)
string(APPEND CMAKE_CXX_FLAGS_INIT
    " -mcpu=generic "
    " -moutline-atomics "
)

# ---------------------------------------------------------------------------
# pkg-config — point at the sysroot's pkg-config database
# ---------------------------------------------------------------------------
set(ENV{PKG_CONFIG_PATH} "")
set(ENV{PKG_CONFIG_LIBDIR}
    "${CMAKE_SYSROOT}/usr/lib/aarch64-linux-gnu/pkgconfig:"
    "${CMAKE_SYSROOT}/usr/lib/pkgconfig:"
    "${CMAKE_SYSROOT}/usr/share/pkgconfig"
)
set(ENV{PKG_CONFIG_SYSROOT_DIR} "${CMAKE_SYSROOT}")

find_program(PKG_CONFIG_EXECUTABLE NAMES "${_cross_prefix}-pkg-config" "pkg-config")
