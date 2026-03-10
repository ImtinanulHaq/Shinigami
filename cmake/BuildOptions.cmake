# =============================================================================
# cmake/BuildOptions.cmake
# All user-facing build options — single source of truth for feature flags.
#
# Every option is documented with its impact and default rationale.
# Do NOT scatter option() calls across subdirectory CMakeLists.txt files.
# =============================================================================

# ---------------------------------------------------------------------------
# Component toggles — fine-grained build control
# ---------------------------------------------------------------------------
option(MW_BUILD_MONITORING  "Build monitord daemon and TUI"               ON)
option(MW_BUILD_PROXY       "Build C++ proxy library and C bindings"      ON)
option(MW_BUILD_SERVICES    "Build hardware service daemons"              ON)
option(MW_BUILD_EXAMPLES    "Build proxy usage examples"                  OFF)
option(MW_BUILD_TESTS       "Build unit and integration tests"            OFF)

# ---------------------------------------------------------------------------
# Hardware subsystem toggles
# (relevant even when MW_BUILD_SERVICES=OFF for HAL layer completeness)
# ---------------------------------------------------------------------------
option(MW_HAL_AUDIO    "Include audio HAL layer (requires ALSA)"          ON)
option(MW_HAL_CAMERA   "Include camera HAL layer (requires V4L2 headers)" ON)
option(MW_HAL_GPIO     "Include GPIO HAL layer"                           ON)
option(MW_HAL_SENSORS  "Include sensor HAL layer"                         ON)

# ---------------------------------------------------------------------------
# Security feature toggles
# Never disable in production; useful for unit-test sandboxes
# ---------------------------------------------------------------------------
option(MW_SECURITY_SANDBOX    "Enable process sandbox (namespaces+cgroups)"  ON)
option(MW_SECURITY_SECCOMP    "Enable per-service seccomp BPF filters"       ON)
option(MW_SECURITY_CAPS       "Enable capabilities hardening"                ON)
option(MW_SECURITY_VERIFY     "Enable HMAC message verification"             ON)

# If the entire security stack is intentionally disabled (e.g. CI tests),
# we'll warn loudly — this is not a silent toggle.
if(NOT MW_SECURITY_SANDBOX OR NOT MW_SECURITY_SECCOMP OR
   NOT MW_SECURITY_CAPS    OR NOT MW_SECURITY_VERIFY)
    message(WARNING
        "[MW] ⚠  One or more security features are DISABLED.\n"
        "     This configuration must NEVER be used in production.\n"
        "     MW_SECURITY_SANDBOX=${MW_SECURITY_SANDBOX}\n"
        "     MW_SECURITY_SECCOMP=${MW_SECURITY_SECCOMP}\n"
        "     MW_SECURITY_CAPS=${MW_SECURITY_CAPS}\n"
        "     MW_SECURITY_VERIFY=${MW_SECURITY_VERIFY}")
endif()

# ---------------------------------------------------------------------------
# Performance / tuning
# ---------------------------------------------------------------------------
option(MW_URING_SQPOLL
    "Enable io_uring SQ_POLL mode (reduces syscall overhead, requires CAP_NET_ADMIN)"
    OFF)

set(MW_SM_MAX_SERVICES "32" CACHE STRING
    "Service manager registry capacity (default: 32)")
set(MW_HAL_REGISTRY_SIZE "128" CACHE STRING
    "HAL device registry capacity — must be power of 2 (default: 128)")
set(MW_POOL_DEFAULT_BLOCKS "8" CACHE STRING
    "Default memory pool block count (default: 8)")

# Validate numeric options
foreach(_opt MW_SM_MAX_SERVICES MW_HAL_REGISTRY_SIZE MW_POOL_DEFAULT_BLOCKS)
    if(NOT ${_opt} MATCHES "^[0-9]+$" OR ${_opt} EQUAL 0)
        message(FATAL_ERROR "${_opt} must be a positive integer, got: '${${_opt}}'")
    endif()
endforeach()

# HAL_REGISTRY_SIZE must be power of two
math(EXPR _hal_reg_check "${MW_HAL_REGISTRY_SIZE} & (${MW_HAL_REGISTRY_SIZE} - 1)")
if(NOT _hal_reg_check EQUAL 0)
    message(FATAL_ERROR
        "MW_HAL_REGISTRY_SIZE must be a power of 2, got: ${MW_HAL_REGISTRY_SIZE}")
endif()

# ---------------------------------------------------------------------------
# LTO — Link-Time Optimisation; enabled by default in Release
# ---------------------------------------------------------------------------
option(MW_ENABLE_LTO
    "Enable Link-Time Optimisation (auto-enabled in Release builds)"
    OFF)

include(CheckIPOSupported)
if(MW_ENABLE_LTO OR CMAKE_BUILD_TYPE STREQUAL "Release")
    check_ipo_supported(RESULT _ipo_ok OUTPUT _ipo_err LANGUAGES C CXX)
    if(_ipo_ok)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
        message(STATUS "[MW] LTO: enabled")
    else()
        message(STATUS "[MW] LTO: not available — ${_ipo_err}")
    endif()
endif()

# ---------------------------------------------------------------------------
# ccache / sccache — transparent compiler caching
# ---------------------------------------------------------------------------
option(MW_USE_CCACHE "Use ccache/sccache for faster incremental rebuilds" ON)
if(MW_USE_CCACHE)
    find_program(_ccache NAMES sccache ccache)
    if(_ccache)
        message(STATUS "[MW] Compiler cache: ${_ccache}")
        set(CMAKE_C_COMPILER_LAUNCHER   "${_ccache}" CACHE FILEPATH "" FORCE)
        set(CMAKE_CXX_COMPILER_LAUNCHER "${_ccache}" CACHE FILEPATH "" FORCE)
    else()
        message(STATUS "[MW] Compiler cache: not found (install ccache for faster builds)")
    endif()
endif()

# ---------------------------------------------------------------------------
# Compiler diagnostics — maximum warnings as errors in CI
# ---------------------------------------------------------------------------
option(MW_WARNINGS_AS_ERRORS
    "Treat all compiler warnings as errors (-Werror / /WX)"
    OFF)

# ---------------------------------------------------------------------------
# Export compile definitions for use by source files via generated header
# ---------------------------------------------------------------------------
set(MW_BUILD_OPTIONS_DEFINED TRUE)  # guard for include-once
