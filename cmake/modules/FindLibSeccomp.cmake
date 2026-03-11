# =============================================================================
# FindLibSeccomp.cmake
# Finds libseccomp — the Linux seccomp BPF filter library.
#
# Imported targets:
#   LibSeccomp::LibSeccomp
# =============================================================================

include(FindPackageHandleStandardArgs)

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(PC_LIBSECCOMP QUIET libseccomp)
endif()

find_path(LibSeccomp_INCLUDE_DIR
    NAMES seccomp.h
    HINTS ${PC_LIBSECCOMP_INCLUDE_DIRS} /usr/include /usr/local/include
)

find_library(LibSeccomp_LIBRARY
    NAMES seccomp
    HINTS
        ${PC_LIBSECCOMP_LIBRARY_DIRS}
        /usr/lib
        /usr/local/lib
        /usr/lib/${CMAKE_SYSTEM_PROCESSOR}-linux-gnu
        /usr/lib64
)

# Extract version from header
if(LibSeccomp_INCLUDE_DIR AND EXISTS "${LibSeccomp_INCLUDE_DIR}/seccomp.h")
    file(STRINGS "${LibSeccomp_INCLUDE_DIR}/seccomp.h" _ver_lines
         REGEX "^#define[ \t]+SCMP_VER_(MAJOR|MINOR|MICRO)[ \t]+[0-9]+")
    foreach(_line ${_ver_lines})
        if(_line MATCHES "SCMP_VER_MAJOR[ \t]+([0-9]+)")
            set(_scmp_major "${CMAKE_MATCH_1}")
        elseif(_line MATCHES "SCMP_VER_MINOR[ \t]+([0-9]+)")
            set(_scmp_minor "${CMAKE_MATCH_1}")
        elseif(_line MATCHES "SCMP_VER_MICRO[ \t]+([0-9]+)")
            set(_scmp_micro "${CMAKE_MATCH_1}")
        endif()
    endforeach()
    if(_scmp_major AND _scmp_minor AND _scmp_micro)
        set(LibSeccomp_VERSION "${_scmp_major}.${_scmp_minor}.${_scmp_micro}")
    endif()
endif()

if(NOT LibSeccomp_VERSION AND PC_LIBSECCOMP_VERSION)
    set(LibSeccomp_VERSION "${PC_LIBSECCOMP_VERSION}")
endif()

find_package_handle_standard_args(LibSeccomp
    REQUIRED_VARS LibSeccomp_LIBRARY LibSeccomp_INCLUDE_DIR
    VERSION_VAR   LibSeccomp_VERSION
    FAIL_MESSAGE  "libseccomp not found. Install: apt-get install libseccomp-dev"
)

if(LibSeccomp_FOUND)
    set(LibSeccomp_INCLUDE_DIRS "${LibSeccomp_INCLUDE_DIR}")
    set(LibSeccomp_LIBRARIES    "${LibSeccomp_LIBRARY}")

    if(NOT TARGET LibSeccomp::LibSeccomp)
        add_library(LibSeccomp::LibSeccomp UNKNOWN IMPORTED)
        set_target_properties(LibSeccomp::LibSeccomp PROPERTIES
            IMPORTED_LOCATION             "${LibSeccomp_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${LibSeccomp_INCLUDE_DIR}"
        )
    endif()
endif()

mark_as_advanced(LibSeccomp_INCLUDE_DIR LibSeccomp_LIBRARY)
