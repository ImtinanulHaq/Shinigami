# =============================================================================
# FindLibUring.cmake
# Finds liburing — the Linux io_uring userspace library.
#
# Input variables:
#   LibUring_FIND_VERSION   — minimum version required
#
# Result variables:
#   LibUring_FOUND          — TRUE if found
#   LibUring_VERSION        — version string from liburing.h
#   LibUring_INCLUDE_DIRS   — include directory path
#   LibUring_LIBRARIES      — library to link
#
# Imported targets:
#   LibUring::LibUring      — use this in target_link_libraries()
# =============================================================================

include(FindPackageHandleStandardArgs)

# ---------------------------------------------------------------------------
# 1. Prefer pkg-config if available
# ---------------------------------------------------------------------------
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(PC_LIBURING QUIET liburing)
endif()

# ---------------------------------------------------------------------------
# 2. Find the header
# ---------------------------------------------------------------------------
find_path(LibUring_INCLUDE_DIR
    NAMES liburing.h
    HINTS
        ${PC_LIBURING_INCLUDE_DIRS}
        /usr/include
        /usr/local/include
    DOC "Path to liburing.h"
)

# ---------------------------------------------------------------------------
# 3. Find the library
# ---------------------------------------------------------------------------
find_library(LibUring_LIBRARY
    NAMES uring liburing
    HINTS
        ${PC_LIBURING_LIBRARY_DIRS}
        /usr/lib
        /usr/local/lib
        /usr/lib/${CMAKE_SYSTEM_PROCESSOR}-linux-gnu
        /usr/lib64
    DOC "Path to liburing.a or liburing.so"
)

# ---------------------------------------------------------------------------
# 4. Extract version from header
# ---------------------------------------------------------------------------
if(LibUring_INCLUDE_DIR AND EXISTS "${LibUring_INCLUDE_DIR}/liburing/io_uring.h")
    # Version is in io_uring.h as IORING_FEAT_* macros, not a simple version.
    # Fall back to pkg-config version.
    set(LibUring_VERSION "${PC_LIBURING_VERSION}")
elseif(PC_LIBURING_VERSION)
    set(LibUring_VERSION "${PC_LIBURING_VERSION}")
else()
    # Last-resort: parse from CMake cache or assume minimum
    set(LibUring_VERSION "2.3")
endif()

# ---------------------------------------------------------------------------
# 5. Handle REQUIRED / version check
# ---------------------------------------------------------------------------
find_package_handle_standard_args(LibUring
    REQUIRED_VARS
        LibUring_LIBRARY
        LibUring_INCLUDE_DIR
    VERSION_VAR
        LibUring_VERSION
    FAIL_MESSAGE
        "liburing not found.\n"
        "Install: apt-get install liburing-dev\n"
        "      or: yum install liburing-devel\n"
        "      or: dnf install liburing-devel\n"
        "Requires Linux kernel 5.1+ and liburing >= 2.3"
)

# ---------------------------------------------------------------------------
# 6. Set output variables
# ---------------------------------------------------------------------------
if(LibUring_FOUND)
    set(LibUring_INCLUDE_DIRS "${LibUring_INCLUDE_DIR}")
    set(LibUring_LIBRARIES    "${LibUring_LIBRARY}")

    if(NOT TARGET LibUring::LibUring)
        add_library(LibUring::LibUring UNKNOWN IMPORTED)
        set_target_properties(LibUring::LibUring PROPERTIES
            IMPORTED_LOCATION             "${LibUring_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${LibUring_INCLUDE_DIR}"
        )
    endif()
endif()

mark_as_advanced(LibUring_INCLUDE_DIR LibUring_LIBRARY)
