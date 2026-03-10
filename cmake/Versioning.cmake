# =============================================================================
# cmake/Versioning.cmake
# Single-source version extraction from a VERSION file.
#
# VERSION file format (semver + optional tweak):
#   MAJOR.MINOR.PATCH[.TWEAK]
#   e.g.  2.1.0  or  2.1.0.3
#
# Usage:
#   mw_extract_version(
#       VERSION_FILE path/to/VERSION
#       OUT_MAJOR    MY_MAJOR
#       OUT_MINOR    MY_MINOR
#       OUT_PATCH    MY_PATCH
#       OUT_TWEAK    MY_TWEAK   # optional — 0 if absent
#       OUT_FULL     MY_FULL    # "MAJOR.MINOR.PATCH" string
#   )
# =============================================================================

function(mw_extract_version)
    cmake_parse_arguments(PARSE_ARGV 0 ARG
        ""
        "VERSION_FILE;OUT_MAJOR;OUT_MINOR;OUT_PATCH;OUT_TWEAK;OUT_FULL"
        ""
    )

    if(NOT ARG_VERSION_FILE)
        message(FATAL_ERROR "mw_extract_version: VERSION_FILE is required")
    endif()

    if(NOT EXISTS "${ARG_VERSION_FILE}")
        message(FATAL_ERROR
            "mw_extract_version: VERSION file not found: ${ARG_VERSION_FILE}\n"
            "Create it with a single semver line, e.g.  1.0.0")
    endif()

    file(READ "${ARG_VERSION_FILE}" _raw)
    string(STRIP "${_raw}" _raw)

    # Remove any comment lines (start with #) and blank lines
    string(REGEX REPLACE "#[^\n]*\n?" "" _raw "${_raw}")
    string(STRIP "${_raw}" _raw)

    if(NOT _raw MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)(\\.[0-9]+)?$")
        message(FATAL_ERROR
            "mw_extract_version: Could not parse VERSION file '${ARG_VERSION_FILE}'.\n"
            "Content: '${_raw}'\n"
            "Expected format: MAJOR.MINOR.PATCH  or  MAJOR.MINOR.PATCH.TWEAK")
    endif()

    set(_major "${CMAKE_MATCH_1}")
    set(_minor "${CMAKE_MATCH_2}")
    set(_patch "${CMAKE_MATCH_3}")
    set(_tweak "0")
    if(CMAKE_MATCH_4)
        string(REGEX REPLACE "^\\." "" _tweak "${CMAKE_MATCH_4}")
    endif()

    set(_full "${_major}.${_minor}.${_patch}")

    if(ARG_OUT_MAJOR) set(${ARG_OUT_MAJOR} "${_major}" PARENT_SCOPE) endif()
    if(ARG_OUT_MINOR) set(${ARG_OUT_MINOR} "${_minor}" PARENT_SCOPE) endif()
    if(ARG_OUT_PATCH) set(${ARG_OUT_PATCH} "${_patch}" PARENT_SCOPE) endif()
    if(ARG_OUT_TWEAK) set(${ARG_OUT_TWEAK} "${_tweak}" PARENT_SCOPE) endif()
    if(ARG_OUT_FULL)  set(${ARG_OUT_FULL}  "${_full}"  PARENT_SCOPE) endif()

    message(STATUS "[MW] Version: ${_full} (tweak=${_tweak})")
endfunction()
