# =============================================================================
# linux-middleware — Root CMakeLists.txt
# =============================================================================
# Minimum version: 3.25 required for:
#   - Full CMakePresets.json v5 support
#   - cmake_language(GET_MESSAGE_LOG_LEVEL)
#   - target_sources(FILE_SET) for header-only installs
#   - Proper SYSTEM include propagation
# =============================================================================

cmake_minimum_required(VERSION 3.25 FATAL_ERROR)

# ---------------------------------------------------------------------------
# Version — single source of truth lives in cmake/Versioning.cmake
# ---------------------------------------------------------------------------
include(cmake/Versioning.cmake)
mw_extract_version(
    VERSION_FILE "${CMAKE_CURRENT_SOURCE_DIR}/VERSION"
    OUT_MAJOR    MW_VERSION_MAJOR
    OUT_MINOR    MW_VERSION_MINOR
    OUT_PATCH    MW_VERSION_PATCH
    OUT_TWEAK    MW_VERSION_TWEAK
    OUT_FULL     MW_VERSION_FULL
)

# ---------------------------------------------------------------------------
# Project declaration
# ---------------------------------------------------------------------------
project(linux_middleware
    VERSION      "${MW_VERSION_FULL}"
    DESCRIPTION  "Embedded Linux hardware middleware platform"
    HOMEPAGE_URL "https://github.com/org/linux-middleware"
    LANGUAGES    C CXX ASM
)

# ---------------------------------------------------------------------------
# Prevent in-source builds — enforced at configure time
# ---------------------------------------------------------------------------
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR
        "In-source builds are not permitted.\n"
        "Create a separate build directory and run cmake from there:\n"
        "  mkdir build && cd build && cmake ..\n"
        "Remove CMakeCache.txt and CMakeFiles/ if they were created.")
endif()

# ---------------------------------------------------------------------------
# CMake module path — our custom Find<Pkg>.cmake modules live here
# ---------------------------------------------------------------------------
list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules")

# ---------------------------------------------------------------------------
# Global CMake policies
# ---------------------------------------------------------------------------
cmake_policy(SET CMP0077 NEW)  # option() honours normal variables
cmake_policy(SET CMP0091 NEW)  # MSVC runtime via abstraction (future-proof)
cmake_policy(SET CMP0092 NEW)  # No /W3 injected by MSVC (future-proof)
cmake_policy(SET CMP0126 NEW)  # set(CACHE) does not remove normal variable

# ---------------------------------------------------------------------------
# Language standards — enforced globally, no fallback
# ---------------------------------------------------------------------------
set(CMAKE_C_STANDARD            17)
set(CMAKE_C_STANDARD_REQUIRED   ON)
set(CMAKE_C_EXTENSIONS          OFF)   # -std=c17, not -std=gnu17

set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)   # -std=c++20, not -std=gnu++20

# ---------------------------------------------------------------------------
# Default build type — RelWithDebInfo if not set by preset or user
# ---------------------------------------------------------------------------
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "[MW] Build type not set — defaulting to RelWithDebInfo")
    set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING
        "Choose: Debug Release RelWithDebInfo MinSizeRel" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
endif()

# ---------------------------------------------------------------------------
# Build output directories — keep build tree clean
# ---------------------------------------------------------------------------
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# Per-config sub-directories (multi-config generators like Ninja Multi-Config)
foreach(_cfg Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${_cfg}" _CFG)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${_CFG} "${CMAKE_BINARY_DIR}/bin/${_cfg}")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${_CFG} "${CMAKE_BINARY_DIR}/lib/${_cfg}")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${_CFG} "${CMAKE_BINARY_DIR}/lib/${_cfg}")
endforeach()

# ---------------------------------------------------------------------------
# Export compile_commands.json — needed by clang-tidy, clangd, IDEs
# ---------------------------------------------------------------------------
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
# Position-independent code — required for shared libraries
# ---------------------------------------------------------------------------
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------------------
# Visibility — hidden by default; symbols are explicitly exported via macros
# This is critical for the proxy shared library ABI hygiene
# ---------------------------------------------------------------------------
set(CMAKE_C_VISIBILITY_PRESET     hidden)
set(CMAKE_CXX_VISIBILITY_PRESET   hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

# ---------------------------------------------------------------------------
# Load our cmake/ infrastructure modules
# ---------------------------------------------------------------------------
include(GNUInstallDirs)              # Standard install path variables
include(CMakePackageConfigHelpers)   # write_basic_package_version_file()
include(GenerateExportHeader)        # generate_export_header()
include(CheckSymbolExists)
include(CheckIncludeFile)
include(CheckCCompilerFlag)
include(CheckCXXCompilerFlag)
include(FeatureSummary)

include(cmake/BuildOptions.cmake)    # User-facing ON/OFF options
include(cmake/Hardening.cmake)       # Compiler security flags
include(cmake/Sanitizers.cmake)      # ASan / UBSan / TSan
include(cmake/StaticAnalysis.cmake)  # clang-tidy / cppcheck / iwyu
include(cmake/Packaging.cmake)       # CPack configuration

# ---------------------------------------------------------------------------
# Detect platform capabilities once — all subdirectories consume these
# ---------------------------------------------------------------------------
find_package(PkgConfig REQUIRED)    # needed by some Find modules below

include(cmake/PlatformChecks.cmake)

# ---------------------------------------------------------------------------
# Find external dependencies — all find_package() calls centralised here.
# Subdirectories link against imported targets; they do NOT call find_package.
# ---------------------------------------------------------------------------

# Threading (pthreads)
find_package(Threads REQUIRED)

# OpenSSL — HMAC-SHA256 for SM protocol and proxy auth
find_package(OpenSSL 3.0 REQUIRED COMPONENTS Crypto SSL)
add_feature_info("OpenSSL" TRUE "Version: ${OPENSSL_VERSION}")

# liburing — LibUring::LibUring IMPORTED target created by PlatformChecks.cmake
# via find_path + find_library. We query pkg-config here only for version info.
pkg_check_modules(PC_LIBURING liburing)   # optional — not REQUIRED, PlatformChecks already validated
add_feature_info("liburing" TRUE "Version: ${PC_LIBURING_VERSION}")

# ALSA — audio HAL
find_package(ALSA REQUIRED)
add_feature_info("ALSA" TRUE "Version: ${ALSA_VERSION_STRING}")

# libseccomp — seccomp BPF filter generation
find_package(LibSeccomp 2.5 REQUIRED)
add_feature_info("libseccomp" TRUE "Version: ${LibSeccomp_VERSION}")

# libcap — Linux capabilities management
find_package(LibCap 2.0 REQUIRED)
add_feature_info("libcap" TRUE "Version: ${LibCap_VERSION}")

# Curses — ncurses for TUI (monitoring)
find_package(Curses REQUIRED)
add_feature_info("ncurses" TRUE "TUI support enabled")

# libbsd (optional) — safer string functions on older glibc
pkg_check_modules(LIBBSD IMPORTED_TARGET libbsd)

# ---------------------------------------------------------------------------
# Generated header — version + build metadata, single copy for the whole tree
# ---------------------------------------------------------------------------
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/version.h.in"
    "${CMAKE_BINARY_DIR}/include/mw/version.h"
    @ONLY
)

# Interface-only target so any subdirectory can do:
#   target_link_libraries(mytarget PRIVATE mw::version)
add_library(mw_version_hdr INTERFACE)
add_library(mw::version ALIAS mw_version_hdr)
target_include_directories(mw_version_hdr
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# ---------------------------------------------------------------------------
# Apply hardening and sanitizer flags globally via compile-options interface
# library. Every target that links mw::compile_opts inherits these flags.
# ---------------------------------------------------------------------------
add_library(mw_compile_opts INTERFACE)
add_library(mw::compile_opts ALIAS mw_compile_opts)
mw_apply_hardening(mw_compile_opts)
mw_apply_sanitizers(mw_compile_opts)

# ---------------------------------------------------------------------------
# Subdirectories — ordered by dependency (leaves first)
# ---------------------------------------------------------------------------
add_subdirectory(dev/core)          # primitives + service_manager
add_subdirectory(dev/hal)           # HAL interface + layer implementations
add_subdirectory(dev/security)      # capabilities, sandbox, seccomp, verify
add_subdirectory(dev/services)      # service daemons (audio/camera/gpio/sensor)
add_subdirectory(dev/proxy)         # C++ proxy library + C bindings
add_subdirectory(dev/monitoring)    # monitord daemon + TUI

# ---------------------------------------------------------------------------
# Install — install rules are in each subdirectory; here we handle the
# CMake package config files so downstream projects can do:
#   find_package(linux_middleware REQUIRED COMPONENTS core hal proxy)
# ---------------------------------------------------------------------------
install(
    TARGETS mw_compile_opts mw_version_hdr
    EXPORT  linux_middleware_targets
)

install(
    FILES "${CMAKE_BINARY_DIR}/include/mw/version.h"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/mw"
)

# Package config files
set(_cmake_install_dir "${CMAKE_INSTALL_LIBDIR}/cmake/linux_middleware")

write_basic_package_version_file(
    "${CMAKE_BINARY_DIR}/cmake/linux_middlewareConfigVersion.cmake"
    VERSION       "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/linux_middlewareConfig.cmake.in"
    "${CMAKE_BINARY_DIR}/cmake/linux_middlewareConfig.cmake"
    INSTALL_DESTINATION "${_cmake_install_dir}"
    PATH_VARS CMAKE_INSTALL_INCLUDEDIR CMAKE_INSTALL_LIBDIR
)

install(
    EXPORT      linux_middleware_targets
    FILE        linux_middlewareTargets.cmake
    NAMESPACE   mw::
    DESTINATION "${_cmake_install_dir}"
)

install(
    FILES
        "${CMAKE_BINARY_DIR}/cmake/linux_middlewareConfig.cmake"
        "${CMAKE_BINARY_DIR}/cmake/linux_middlewareConfigVersion.cmake"
    DESTINATION "${_cmake_install_dir}"
)

# ---------------------------------------------------------------------------
# Print feature summary at end of configure
# ---------------------------------------------------------------------------
message(STATUS "")
message(STATUS "========================================================")
message(STATUS " linux-middleware ${PROJECT_VERSION}")
message(STATUS " Build type : ${CMAKE_BUILD_TYPE}")
message(STATUS " C compiler : ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}")
message(STATUS " C++ compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS " Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "========================================================")
feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
