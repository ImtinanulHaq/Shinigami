# =============================================================================
# cmake/PlatformChecks.cmake
# One-time platform capability probes. Results consumed throughout the tree.
# All checks go here — no check_symbol_exists() scattered in subdirectories.
# =============================================================================

include_guard(GLOBAL)

include(CheckSymbolExists)
include(CheckIncludeFile)
include(CheckFunctionExists)
include(CheckStructHasMember)
include(CheckTypeSize)

# ---------------------------------------------------------------------------
# io_uring probes
# ---------------------------------------------------------------------------
check_include_file("liburing.h"          HAVE_LIBURING_H)
check_include_file("linux/io_uring.h"    HAVE_LINUX_IO_URING_H)

# Minimum kernel ABI checks for io_uring operations we use
check_symbol_exists(io_uring_queue_init   "liburing.h"  HAVE_IO_URING_QUEUE_INIT)
check_symbol_exists(io_uring_prep_read    "liburing.h"  HAVE_IO_URING_PREP_READ)
check_symbol_exists(io_uring_prep_poll_add "liburing.h" HAVE_IO_URING_PREP_POLL_ADD)

if(NOT HAVE_LIBURING_H OR NOT HAVE_IO_URING_QUEUE_INIT)
    message(FATAL_ERROR
        "[MW] liburing.h or io_uring API not found.\n"
        "     Requires: Linux 5.1+ kernel + liburing 2.3+\n"
        "     Install:  apt-get install liburing-dev\n"
        "               or: yum install liburing-devel")
endif()

# ---------------------------------------------------------------------------
# POSIX / glibc probes
# ---------------------------------------------------------------------------
check_symbol_exists(getrandom       "sys/random.h"  HAVE_GETRANDOM)
check_symbol_exists(memfd_create    "sys/mman.h"    HAVE_MEMFD_CREATE)
check_symbol_exists(accept4         "sys/socket.h"  HAVE_ACCEPT4)
check_symbol_exists(pipe2           "unistd.h"      HAVE_PIPE2)
check_symbol_exists(eventfd         "sys/eventfd.h" HAVE_EVENTFD)
check_symbol_exists(signalfd        "sys/signalfd.h" HAVE_SIGNALFD)
check_symbol_exists(timerfd_create  "sys/timerfd.h" HAVE_TIMERFD)
check_symbol_exists(shm_open        "sys/mman.h"    HAVE_SHM_OPEN)
check_symbol_exists(madvise         "sys/mman.h"    HAVE_MADVISE)
check_symbol_exists(explicit_bzero  "string.h"      HAVE_EXPLICIT_BZERO)

# accept4 is critical — we use it for atomic SOCK_CLOEXEC
if(NOT HAVE_ACCEPT4)
    message(FATAL_ERROR "[MW] accept4() not found — requires Linux 2.6.28+ / glibc 2.10+")
endif()
if(NOT HAVE_GETRANDOM)
    message(FATAL_ERROR "[MW] getrandom() not found — requires Linux 3.17+ / glibc 2.25+")
endif()

# explicit_bzero is security-critical — warn if absent (some musl versions lack it)
if(NOT HAVE_EXPLICIT_BZERO)
    message(WARNING "[MW] explicit_bzero() not found — using memset fallback (not optimal)")
endif()

# ---------------------------------------------------------------------------
# SO_PEERCRED — used by SM for kernel-verified PID auth
# ---------------------------------------------------------------------------
check_struct_has_member("struct ucred" "pid" "sys/socket.h" HAVE_UCRED_STRUCT)
if(NOT HAVE_UCRED_STRUCT)
    message(FATAL_ERROR
        "[MW] struct ucred / SO_PEERCRED not available.\n"
        "     The service manager requires Linux socket peer credential support.")
endif()

# ---------------------------------------------------------------------------
# seccomp probes
# ---------------------------------------------------------------------------
check_include_file("linux/seccomp.h"   HAVE_LINUX_SECCOMP_H)
check_include_file("seccomp.h"         HAVE_SECCOMP_H)

# ---------------------------------------------------------------------------
# Linux capabilities
# ---------------------------------------------------------------------------
check_include_file("sys/capability.h"  HAVE_SYS_CAPABILITY_H)

# ---------------------------------------------------------------------------
# V4L2 camera
# ---------------------------------------------------------------------------
check_include_file("linux/videodev2.h" HAVE_V4L2_H)
check_symbol_exists(VIDIOC_QUERYCAP "linux/videodev2.h" HAVE_VIDIOC_QUERYCAP)

# ---------------------------------------------------------------------------
# IIO sensor interface
# ---------------------------------------------------------------------------
check_include_file("linux/iio/types.h" HAVE_IIO_TYPES_H)

# ---------------------------------------------------------------------------
# Atomics — we require C11 _Atomic
# ---------------------------------------------------------------------------
set(CMAKE_EXTRA_INCLUDE_FILES "stdatomic.h")
check_type_size("_Atomic int" HAVE_C11_ATOMICS LANGUAGE C)
unset(CMAKE_EXTRA_INCLUDE_FILES)
if(NOT HAVE_C11_ATOMICS)
    message(FATAL_ERROR
        "[MW] C11 _Atomic not available.\n"
        "     Requires: GCC 4.9+ or Clang 3.6+ with C17 mode.")
endif()

# ---------------------------------------------------------------------------
# Cache line size — used by ring_buffer padding
# ---------------------------------------------------------------------------
if(NOT DEFINED CACHE_LINE_SIZE)
    # Try to read from sysfs at configure time
    if(EXISTS "/sys/devices/system/cpu/cpu0/cache/index0/coherency_line_size")
        file(READ "/sys/devices/system/cpu/cpu0/cache/index0/coherency_line_size"
             _cls_raw)
        string(STRIP "${_cls_raw}" _cls_raw)
        if(_cls_raw MATCHES "^[0-9]+$")
            set(CACHE_LINE_SIZE "${_cls_raw}" CACHE INTERNAL "CPU cache line size")
        endif()
    endif()
    if(NOT CACHE_LINE_SIZE)
        set(CACHE_LINE_SIZE 64 CACHE INTERNAL "CPU cache line size (assumed)")
    endif()
endif()
message(STATUS "[MW] Cache line size: ${CACHE_LINE_SIZE} bytes")

# ---------------------------------------------------------------------------
# Expose all checks as compile definitions on a reusable INTERFACE target
# ---------------------------------------------------------------------------
add_library(mw_platform_checks INTERFACE)
add_library(mw::platform_checks ALIAS mw_platform_checks)

target_compile_definitions(mw_platform_checks INTERFACE
    $<$<BOOL:${HAVE_EXPLICIT_BZERO}>:   MW_HAVE_EXPLICIT_BZERO=1>
    $<$<BOOL:${HAVE_MEMFD_CREATE}>:     MW_HAVE_MEMFD_CREATE=1>
    $<$<BOOL:${HAVE_GETRANDOM}>:        MW_HAVE_GETRANDOM=1>
    $<$<BOOL:${HAVE_IIO_TYPES_H}>:      MW_HAVE_IIO=1>
    $<$<BOOL:${HAVE_V4L2_H}>:           MW_HAVE_V4L2=1>
    MW_CACHE_LINE_SIZE=${CACHE_LINE_SIZE}
)
