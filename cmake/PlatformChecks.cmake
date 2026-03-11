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
# io_uring — find header + library, create LibUring::LibUring imported target
#
# DESIGN: We deliberately skip check_c_source_compiles / check_symbol_exists
# for liburing because:
#   1. io_uring_* functions are static inline — not exported symbols in .so
#   2. check_c_source_compiles inherits CMAKE_C_EXTENSIONS=OFF (-std=c17)
#      but liburing.h needs _GNU_SOURCE for kernel types (__kernel_rwf_t etc)
#   3. find_path + find_library is sufficient evidence of correct installation
#   4. PC_LIBURING_* vars from pkg_check_modules may be empty at this point
#      if PkgConfig runs after this include (root CMakeLists ordering)
#
# If find_path and find_library both succeed → the package is installed and
# usable. We trust the distro package manager (pacman / apt) to ship a
# consistent, complete package.
# ---------------------------------------------------------------------------
find_path(_liburing_include_dir
    NAMES liburing.h
    PATHS /usr/include /usr/local/include /opt/include
    NO_DEFAULT_PATH)
if(NOT _liburing_include_dir)
    find_path(_liburing_include_dir NAMES liburing.h)
endif()

find_library(_liburing_path
    NAMES uring
    PATHS /usr/lib /usr/lib64 /usr/local/lib
          /usr/lib/${CMAKE_SYSTEM_PROCESSOR}-linux-gnu
    NO_DEFAULT_PATH)
if(NOT _liburing_path)
    find_library(_liburing_path NAMES uring)
endif()

if(NOT _liburing_include_dir OR NOT _liburing_path)
    message(FATAL_ERROR
        "[MW] liburing not found.\n"
        "     Header:  ${_liburing_include_dir}\n"
        "     Library: ${_liburing_path}\n"
        "     Install: pacman -S liburing  /  apt-get install liburing-dev")
endif()

message(STATUS "[MW] liburing header : ${_liburing_include_dir}")
message(STATUS "[MW] liburing library: ${_liburing_path}")

# Set feature flags — confirmed by successful find_path + find_library
set(HAVE_LIBURING_H          TRUE CACHE BOOL "liburing.h found" FORCE)
set(HAVE_IO_URING_QUEUE_INIT TRUE CACHE BOOL "io_uring API available" FORCE)
set(HAVE_IO_URING_PREP_READ  TRUE CACHE BOOL "io_uring API available" FORCE)
set(HAVE_IO_URING_PREP_POLL_ADD TRUE CACHE BOOL "io_uring API available" FORCE)

# Create LibUring::LibUring imported target so dev/core/CMakeLists.txt can
# do: target_link_libraries(mw_core PUBLIC LibUring::LibUring)
# This MUST exist — mw_core links against it.
if(NOT TARGET LibUring::LibUring)
    add_library(LibUring::LibUring UNKNOWN IMPORTED GLOBAL)
    set_target_properties(LibUring::LibUring PROPERTIES
        IMPORTED_LOCATION             "${_liburing_path}"
        INTERFACE_INCLUDE_DIRECTORIES "${_liburing_include_dir}"
        # liburing.h needs _GNU_SOURCE for __kernel_rwf_t and other kernel types.
        # Propagate it to every target that links LibUring::LibUring so they
        # compile cleanly even with CMAKE_C_EXTENSIONS=OFF (-std=c17).
        INTERFACE_COMPILE_DEFINITIONS "_GNU_SOURCE"
    )
endif()

check_include_file("linux/io_uring.h" HAVE_LINUX_IO_URING_H)

# ---------------------------------------------------------------------------
# POSIX / glibc probes
# All GNU extensions — must define _GNU_SOURCE or they're invisible
# ---------------------------------------------------------------------------
set(CMAKE_REQUIRED_DEFINITIONS -D_GNU_SOURCE)
check_symbol_exists(getrandom       "sys/random.h"   HAVE_GETRANDOM)
check_symbol_exists(memfd_create    "sys/mman.h"     HAVE_MEMFD_CREATE)
check_symbol_exists(accept4         "sys/socket.h"   HAVE_ACCEPT4)
check_symbol_exists(pipe2           "unistd.h"       HAVE_PIPE2)
check_symbol_exists(eventfd         "sys/eventfd.h"  HAVE_EVENTFD)
check_symbol_exists(signalfd        "sys/signalfd.h" HAVE_SIGNALFD)
check_symbol_exists(timerfd_create  "sys/timerfd.h"  HAVE_TIMERFD)
check_symbol_exists(shm_open        "sys/mman.h"     HAVE_SHM_OPEN)
check_symbol_exists(madvise         "sys/mman.h"     HAVE_MADVISE)
check_symbol_exists(explicit_bzero  "string.h"       HAVE_EXPLICIT_BZERO)

if(NOT HAVE_ACCEPT4)
    message(FATAL_ERROR "[MW] accept4() not found — requires Linux 2.6.28+ / glibc 2.10+")
endif()
if(NOT HAVE_GETRANDOM)
    message(FATAL_ERROR "[MW] getrandom() not found — requires Linux 3.17+ / glibc 2.25+")
endif()
if(NOT HAVE_EXPLICIT_BZERO)
    message(WARNING "[MW] explicit_bzero() not found — using memset fallback (not optimal)")
endif()

# ---------------------------------------------------------------------------
# SO_PEERCRED — kernel-verified PID/UID auth for SM socket
# _GNU_SOURCE must still be set here (struct ucred is a GNU extension)
# ---------------------------------------------------------------------------
check_struct_has_member("struct ucred" "pid" "sys/socket.h" HAVE_UCRED_STRUCT)
unset(CMAKE_REQUIRED_DEFINITIONS)

if(NOT HAVE_UCRED_STRUCT)
    message(FATAL_ERROR
        "[MW] struct ucred / SO_PEERCRED not available.\n"
        "     The service manager requires Linux socket peer credential support.")
endif()

# ---------------------------------------------------------------------------
# seccomp + capabilities headers
# ---------------------------------------------------------------------------
check_include_file("linux/seccomp.h"   HAVE_LINUX_SECCOMP_H)
check_include_file("seccomp.h"         HAVE_SECCOMP_H)
check_include_file("sys/capability.h"  HAVE_SYS_CAPABILITY_H)

# ---------------------------------------------------------------------------
# V4L2 camera + IIO sensor
# ---------------------------------------------------------------------------
check_include_file("linux/videodev2.h" HAVE_V4L2_H)
check_symbol_exists(VIDIOC_QUERYCAP "linux/videodev2.h" HAVE_VIDIOC_QUERYCAP)
check_include_file("linux/iio/types.h" HAVE_IIO_TYPES_H)

# ---------------------------------------------------------------------------
# C11 atomics — required, no fallback
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
# Cache line size — read from sysfs at configure time
# ---------------------------------------------------------------------------
if(NOT DEFINED CACHE_LINE_SIZE)
    if(EXISTS "/sys/devices/system/cpu/cpu0/cache/index0/coherency_line_size")
        file(READ "/sys/devices/system/cpu/cpu0/cache/index0/coherency_line_size" _cls_raw)
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
# INTERFACE target — all compile-time feature flags in one place
# Every target links mw::platform_checks to get these definitions.
# ---------------------------------------------------------------------------
add_library(mw_platform_checks INTERFACE)
add_library(mw::platform_checks ALIAS mw_platform_checks)

target_compile_definitions(mw_platform_checks INTERFACE
    _GNU_SOURCE                                                         # required globally — liburing + kernel headers
    $<$<BOOL:${HAVE_EXPLICIT_BZERO}>:   MW_HAVE_EXPLICIT_BZERO=1>
    $<$<BOOL:${HAVE_MEMFD_CREATE}>:     MW_HAVE_MEMFD_CREATE=1>
    $<$<BOOL:${HAVE_GETRANDOM}>:        MW_HAVE_GETRANDOM=1>
    $<$<BOOL:${HAVE_IIO_TYPES_H}>:      MW_HAVE_IIO=1>
    $<$<BOOL:${HAVE_V4L2_H}>:           MW_HAVE_V4L2=1>
    MW_CACHE_LINE_SIZE=${CACHE_LINE_SIZE}
)
