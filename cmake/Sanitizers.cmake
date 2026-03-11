# =============================================================================
# cmake/Sanitizers.cmake
# Sanitizer support — ASan, UBSan, TSan, MSan, LSan.
#
# Activated via CMakePresets.json or cmake -DMW_SANITIZE=<type>.
# Never enabled in Release builds — enforced below.
# =============================================================================

include_guard(GLOBAL)

# Valid sanitizer modes
set(MW_SANITIZE "" CACHE STRING
    "Sanitizer to enable: asan | ubsan | tsan | msan | asan+ubsan | none (default: none)")
set_property(CACHE MW_SANITIZE PROPERTY STRINGS
    "" "asan" "ubsan" "tsan" "msan" "asan+ubsan" "lsan")

function(mw_apply_sanitizers target)

    if(NOT MW_SANITIZE OR MW_SANITIZE STREQUAL "none")
        return()
    endif()

    # Sanitizers must not be used in release builds — they add significant
    # overhead and disable important optimisations
    if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
        message(FATAL_ERROR
            "[MW] Sanitizers cannot be enabled in Release/MinSizeRel builds.\n"
            "     Use 'Debug' or 'RelWithDebInfo' build type with sanitizers.")
    endif()

    # TSan is incompatible with ASan/LSan/MSan
    if(MW_SANITIZE MATCHES "tsan" AND MW_SANITIZE MATCHES "asan|msan|lsan")
        message(FATAL_ERROR
            "[MW] TSan is incompatible with ASan/MSan/LSan.\n"
            "     Set MW_SANITIZE to exactly one of: tsan, asan, ubsan, msan, asan+ubsan")
    endif()

    # MSan requires Clang (GCC MSan support is incomplete)
    if(MW_SANITIZE STREQUAL "msan" AND NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        message(FATAL_ERROR
            "[MW] MemorySanitizer (msan) requires Clang.\n"
            "     Current compiler: ${CMAKE_CXX_COMPILER_ID}")
    endif()

    set(_san_flags "")
    set(_san_defines "")

    # ── AddressSanitizer ──────────────────────────────────────────────────
    if(MW_SANITIZE MATCHES "asan")
        list(APPEND _san_flags
            "-fsanitize=address"
            "-fno-omit-frame-pointer"
            "-fno-optimize-sibling-calls"
            "-fsanitize-address-use-after-scope"
        )
        # Detect use-after-return (slow but thorough)
        # list(APPEND _san_flags "-fsanitize-address-use-after-return=always")
        list(APPEND _san_defines MW_SANITIZE_ASAN=1)
        message(STATUS "[MW] Sanitizers: AddressSanitizer enabled")
    endif()

    # ── UndefinedBehaviourSanitizer ───────────────────────────────────────
    if(MW_SANITIZE MATCHES "ubsan")
        list(APPEND _san_flags
            "-fsanitize=undefined"
            "-fsanitize=implicit-conversion"
            "-fsanitize=nullability"
            "-fsanitize=integer"
            "-fno-omit-frame-pointer"
            # Emit a diagnostic but don't trap — lets us see all errors
            "-fsanitize-recover=all"
        )
        list(APPEND _san_defines MW_SANITIZE_UBSAN=1)
        message(STATUS "[MW] Sanitizers: UndefinedBehaviourSanitizer enabled")
    endif()

    # ── ThreadSanitizer ───────────────────────────────────────────────────
    if(MW_SANITIZE MATCHES "tsan")
        list(APPEND _san_flags
            "-fsanitize=thread"
            "-fno-omit-frame-pointer"
        )
        list(APPEND _san_defines MW_SANITIZE_TSAN=1)
        message(STATUS "[MW] Sanitizers: ThreadSanitizer enabled")
    endif()

    # ── MemorySanitizer (Clang only) ──────────────────────────────────────
    if(MW_SANITIZE STREQUAL "msan")
        list(APPEND _san_flags
            "-fsanitize=memory"
            "-fsanitize-memory-track-origins=2"
            "-fno-omit-frame-pointer"
        )
        list(APPEND _san_defines MW_SANITIZE_MSAN=1)
        message(STATUS "[MW] Sanitizers: MemorySanitizer enabled")
    endif()

    # ── LeakSanitizer standalone ──────────────────────────────────────────
    if(MW_SANITIZE STREQUAL "lsan")
        list(APPEND _san_flags "-fsanitize=leak")
        list(APPEND _san_defines MW_SANITIZE_LSAN=1)
        message(STATUS "[MW] Sanitizers: LeakSanitizer enabled")
    endif()

    target_compile_options(${target} INTERFACE ${_san_flags})
    target_link_options(${target}    INTERFACE ${_san_flags})
    target_compile_definitions(${target} INTERFACE ${_san_defines})

    # Ensure frame pointer is always kept — required for meaningful stack traces
    target_compile_options(${target} INTERFACE "-fno-omit-frame-pointer")

endfunction()
