# =============================================================================
# FindLibCap.cmake
# Finds libcap — Linux capabilities library.
#
# Imported targets:
#   LibCap::LibCap
# =============================================================================

include(FindPackageHandleStandardArgs)

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(PC_LIBCAP QUIET libcap)
endif()

find_path(LibCap_INCLUDE_DIR
    NAMES sys/capability.h
    HINTS ${PC_LIBCAP_INCLUDE_DIRS} /usr/include /usr/local/include
)

find_library(LibCap_LIBRARY
    NAMES cap
    HINTS
        ${PC_LIBCAP_LIBRARY_DIRS}
        /usr/lib
        /usr/local/lib
        /usr/lib/${CMAKE_SYSTEM_PROCESSOR}-linux-gnu
        /usr/lib64
)

if(LibCap_INCLUDE_DIR AND EXISTS "${LibCap_INCLUDE_DIR}/sys/capability.h")
    file(STRINGS "${LibCap_INCLUDE_DIR}/sys/capability.h" _ver_line
         REGEX "^#define[ \t]+_LIBCAP_VERSION[ \t]+")
    if(_ver_line MATCHES "_LIBCAP_VERSION[ \t]+([0-9.]+)")
        set(LibCap_VERSION "${CMAKE_MATCH_1}")
    endif()
endif()
if(NOT LibCap_VERSION AND PC_LIBCAP_VERSION)
    set(LibCap_VERSION "${PC_LIBCAP_VERSION}")
endif()

find_package_handle_standard_args(LibCap
    REQUIRED_VARS LibCap_LIBRARY LibCap_INCLUDE_DIR
    VERSION_VAR   LibCap_VERSION
    FAIL_MESSAGE  "libcap not found. Install: apt-get install libcap-dev"
)

if(LibCap_FOUND)
    set(LibCap_INCLUDE_DIRS "${LibCap_INCLUDE_DIR}")
    set(LibCap_LIBRARIES    "${LibCap_LIBRARY}")
    if(NOT TARGET LibCap::LibCap)
        add_library(LibCap::LibCap UNKNOWN IMPORTED)
        set_target_properties(LibCap::LibCap PROPERTIES
            IMPORTED_LOCATION             "${LibCap_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${LibCap_INCLUDE_DIR}"
        )
    endif()
endif()

mark_as_advanced(LibCap_INCLUDE_DIR LibCap_LIBRARY)
