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
# Step 1 — find the header directory explicitly so CMAKE_REQUIRED_INCLUDES
# is populated *before* any check_include_file / check_symbol_exists runs.
# cmake's default header search can miss distro-specific paths when run as root.
find_path(_liburing_include_dir
    NAMES liburing.h
    PATHS /usr/include /usr/local/include /opt/include
          ${PC_LIBURING_INCLUDE_DIRS}
    NO_DEFAULT_PATH)
# Fallback: let cmake search its own default paths too
if(NOT _liburing_include_dir)
    find_path(_liburing_include_dir NAMES liburing.h)
endif()

# Step 2 — find the library
find_library(_liburing_path
    NAMES uring
    PATHS /usr/lib /usr/lib64 /usr/local/lib
          /usr/lib/${CMAKE_SYSTEM_PROCESSOR}-linux-gnu
          ${PC_LIBURING_LIBRARY_DIRS}
    NO_DEFAULT_PATH)
if(NOT _liburing_path)
    find_library(_liburing_path NAMES uring)
endif()

if(NOT _liburing_include_dir OR NOT _liburing_path)
    message(FATAL_ERROR
        "[MW] liburing not found.\n"
        "     Header dir: ${_liburing_include_dir}\n"
        "     Library:    ${_liburing_path}\n"
        "     Install:    pacman -S liburing  /  apt-get install liburing-dev")
endif()

message(STATUS "[MW] liburing header: ${_liburing_include_dir}")
message(STATUS "[MW] liburing lib:    ${_liburing_path}")

# Step 3 — find_path already confirmed the header exists, so set HAVE_LIBURING_H
# directly. Rerunning check_include_file is redundant and unreliable across cmake
# versions when the path is non-standard. Use CMAKE_REQUIRED_FLAGS for -I injection
# into check_symbol_exists (more portable than CMAKE_REQUIRED_INCLUDES).
# io_uring_queue_init and friends are STATIC INLINE functions defined entirely
# in liburing.h — they are NOT exported symbols in liburing.so.
# check_symbol_exists only searches the binary, so it always fails for these.
# We must use check_c_source_compiles to actually compile a call to them.
set(HAVE_LIBURING_H TRUE CACHE BOOL "liburing.h present (verified by find_path)" FORCE)

check_include_file("linux/io_uring.h" HAVE_LINUX_IO_URING_H)

include(CheckCSourceCompiles)
set(CMAKE_REQUIRED_FLAGS    "-I${_liburing_include_dir}")
set(CMAKE_REQUIRED_LIBRARIES "${_liburing_path}")

check_c_source_compiles("
#include <liburing.h>
int main(void) {
    struct io_uring ring;
    io_uring_queue_init(8, &ring, 0);
    struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    io_uring_prep_read(sqe, 0, NULL, 0, 0);
    io_uring_prep_poll_add(sqe, 0, 0);
    io_uring_queue_exit(&ring);
    return 0;
}
" HAVE_IO_URING_API)

unset(CMAKE_REQUIRED_FLAGS)
unset(CMAKE_REQUIRED_LIBRARIES)

if(NOT HAVE_IO_URING_API)
    message(FATAL_ERROR
        "[MW] liburing.h found at ${_liburing_include_dir} but failed to compile io_uring API.\n"
        "     Requires: Linux 5.1+ kernel + liburing 2.3+\n"
        "     Install:  pacman -S liburing  /  apt-get install liburing-dev")
endif()

# Expose individual feature flags for code that checks them
set(HAVE_IO_URING_QUEUE_INIT    TRUE CACHE BOOL "" FORCE)
set(HAVE_IO_URING_PREP_READ     TRUE CACHE BOOL "" FORCE)
set(HAVE_IO_URING_PREP_POLL_ADD TRUE CACHE BOOL "" FORCE)

# ---------------------------------------------------------------------------
# POSIX / glibc probes
# ---------------------------------------------------------------------------
# These are all GNU extensions hidden behind _GNU_SOURCE in the headers.
# Without this, check_symbol_exists won't see them even on a fully capable system.
set(CMAKE_REQUIRED_DEFINITIONS -D_GNU_SOURCE)
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
unset(CMAKE_REQUIRED_DEFINITIONS)
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
