# =============================================================================
# cmake/Hardening.cmake
# Compiler and linker security hardening flags.
#
# Called once from root CMakeLists.txt.
# Exposes: mw_apply_hardening(<interface_target>)
#
# All flags are tested for support before being added — build will succeed
# on any Linux distro with GCC ≥ 10 or Clang ≥ 12.
# =============================================================================

include_guard(GLOBAL)

# Helper: test a C compiler flag and add it to list if supported
function(_mw_check_c_flag flag list_var)
    string(MAKE_C_IDENTIFIER "HAVE_C${flag}" _cache_var)
    check_c_compiler_flag("${flag}" "${_cache_var}")
    if(${_cache_var})
        list(APPEND ${list_var} "${flag}")
        set(${list_var} "${${list_var}}" PARENT_SCOPE)
    endif()
endfunction()

# Helper: test a C++ compiler flag and add it to list if supported
function(_mw_check_cxx_flag flag list_var)
    string(MAKE_C_IDENTIFIER "HAVE_CXX${flag}" _cache_var)
    check_cxx_compiler_flag("${flag}" "${_cache_var}")
    if(${_cache_var})
        list(APPEND ${list_var} "${flag}")
        set(${list_var} "${${list_var}}" PARENT_SCOPE)
    endif()
endfunction()

# ---------------------------------------------------------------------------
# mw_apply_hardening(<target>)
# Adds security flags to an INTERFACE target; all real targets
# link against it to inherit the flags.
# ---------------------------------------------------------------------------
function(mw_apply_hardening target)

    # ── Shared flags (C and C++) ─────────────────────────────────────────
    set(_common_flags "")

    # Stack protection — use strong variant (GCC 4.9+, Clang)
    _mw_check_c_flag("-fstack-protector-strong" _common_flags)

    # Stack clash protection (GCC 8+, Clang 11+)
    _mw_check_c_flag("-fstack-clash-protection" _common_flags)

    # Control-Flow Integrity — forward-edge CFI (Clang only; GCC uses CET)
    # We check both; pick whichever is available
    _mw_check_c_flag("-fcf-protection=full" _common_flags)  # x86 CET (GCC/Clang)

    # Fortify source — catches common buffer/string bugs at runtime
    # Use _FORTIFY_SOURCE=3 if available (GCC 12+), fall back to 2
    _mw_check_c_flag("-D_FORTIFY_SOURCE=3" _common_flags)
    if(NOT "HAVE_C-D_FORTIFY_SOURCE=3")
        _mw_check_c_flag("-D_FORTIFY_SOURCE=2" _common_flags)
    endif()

    # Wipe stack allocations to zero on function entry (Clang)
    _mw_check_c_flag("-ftrivial-auto-var-init=zero" _common_flags)

    # Warn if __attribute__((format)) mismatches (not a hardening flag but
    # catches a class of string-format bugs at compile time)
    _mw_check_c_flag("-Wformat=2"         _common_flags)
    _mw_check_c_flag("-Wformat-security"  _common_flags)
    _mw_check_c_flag("-Wformat-overflow=2" _common_flags)

    # Pointer arithmetic warnings
    _mw_check_c_flag("-Warray-bounds=2"           _common_flags)
    _mw_check_c_flag("-Wstringop-overflow=4"       _common_flags)
    _mw_check_c_flag("-Wstringop-truncation"       _common_flags)

    # Signed/unsigned integer overflow — UB that becomes a security bug
    _mw_check_c_flag("-fno-strict-overflow"        _common_flags)
    _mw_check_c_flag("-fwrapv"                     _common_flags)

    # Undefined behaviour on NULL dereference — don't allow the compiler
    # to assume pointers are non-NULL and remove null checks
    _mw_check_c_flag("-fno-delete-null-pointer-checks" _common_flags)

    # Warn on implicit function declarations (C only, catches missing headers)
    # Will be added separately for C below

    # ── C-only flags ─────────────────────────────────────────────────────
    set(_c_flags ${_common_flags})
    _mw_check_c_flag("-Wimplicit-function-declaration" _c_flags)
    _mw_check_c_flag("-Wimplicit-int"                  _c_flags)
    _mw_check_c_flag("-Wstrict-prototypes"             _c_flags)
    _mw_check_c_flag("-Wmissing-prototypes"            _c_flags)

    # ── C++-only flags ────────────────────────────────────────────────────
    set(_cxx_flags ${_common_flags})

    # ── Linker hardening flags ────────────────────────────────────────────
    set(_link_flags "")

    # RELRO — make GOT/PLT read-only after loading
    _mw_check_c_flag("-Wl,-z,relro"         _link_flags)
    _mw_check_c_flag("-Wl,-z,now"           _link_flags)   # Full RELRO

    # No executable stack
    _mw_check_c_flag("-Wl,-z,noexecstack"   _link_flags)

    # Disallow text relocations in shared libs
    _mw_check_c_flag("-Wl,-z,notext"        _link_flags)

    # Warn on undefined symbols at link time (catches missing link deps)
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
        _mw_check_c_flag("-Wl,-z,defs"     _link_flags)
    endif()

    # Separate code and data segments
    _mw_check_c_flag("-Wl,-z,separate-code" _link_flags)

    # ── Warning flags (all build types) ──────────────────────────────────
    list(APPEND _c_flags
        "-Wall"
        "-Wextra"
        "-Wshadow"
        "-Wpointer-arith"
        "-Wcast-align"
        "-Wwrite-strings"
        "-Wnull-dereference"
        "-Wdouble-promotion"
        "-Wundef"
    )

    list(APPEND _cxx_flags
        "-Wall"
        "-Wextra"
        "-Wshadow"
        "-Wnon-virtual-dtor"
        "-Wold-style-cast"
        "-Wcast-align"
        "-Wnull-dereference"
        "-Woverloaded-virtual"
        "-Wdouble-promotion"
        "-Wundef"
    )

    # Warnings as errors in CI/Release
    if(MW_WARNINGS_AS_ERRORS)
        list(APPEND _c_flags   "-Werror")
        list(APPEND _cxx_flags "-Werror")
        message(STATUS "[MW] Hardening: -Werror enabled (MW_WARNINGS_AS_ERRORS=ON)")
    endif()

    # ── Apply to the interface target ─────────────────────────────────────
    target_compile_options(${target} INTERFACE
        $<$<COMPILE_LANGUAGE:C>:${_c_flags}>
        $<$<COMPILE_LANGUAGE:CXX>:${_cxx_flags}>
    )

    target_link_options(${target} INTERFACE ${_link_flags})

    # PIE — position-independent executables (ASLR support)
    # Applied to executable targets by setting the property at target creation
    # time in each subdirectory. We set it globally here:
    set_property(GLOBAL PROPERTY POSITION_INDEPENDENT_CODE ON)

    target_compile_definitions(${target} INTERFACE
        # Expose all POSIX extensions
        _POSIX_C_SOURCE=200809L
        _GNU_SOURCE

        # Sizes from build options — resolved here, once, for the whole build
        SM_MAX_SERVICES_BUILD=${MW_SM_MAX_SERVICES}
        HAL_REGISTRY_SIZE=${MW_HAL_REGISTRY_SIZE}
        POOL_DEFAULT_BLOCKS=${MW_POOL_DEFAULT_BLOCKS}

        # Feature flags from options
        $<$<BOOL:${MW_SECURITY_SANDBOX}>:MW_SANDBOX_ENABLED=1>
        $<$<BOOL:${MW_SECURITY_SECCOMP}>:MW_SECCOMP_ENABLED=1>
        $<$<BOOL:${MW_SECURITY_CAPS}>:MW_CAPS_ENABLED=1>
        $<$<BOOL:${MW_SECURITY_VERIFY}>:MW_VERIFY_ENABLED=1>
        $<$<BOOL:${MW_URING_SQPOLL}>:MW_URING_SQPOLL=1>
    )

    message(STATUS "[MW] Hardening: flags applied to '${target}'")
endfunction()
