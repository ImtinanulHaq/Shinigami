# =============================================================================
# cmake/StaticAnalysis.cmake
# Static analysis tooling: clang-tidy, cppcheck, include-what-you-use (iwyu).
#
# Activated via options or presets. Does NOT silently do nothing —
# if you ask for clang-tidy and it's not found, configure fails.
# =============================================================================

include_guard(GLOBAL)

option(MW_CLANG_TIDY   "Run clang-tidy on all C/C++ sources"  OFF)
option(MW_CPPCHECK     "Run cppcheck on all C sources"        OFF)
option(MW_IWYU         "Run include-what-you-use"             OFF)

# ---------------------------------------------------------------------------
# clang-tidy
# ---------------------------------------------------------------------------
if(MW_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE
        NAMES "clang-tidy-17" "clang-tidy-16" "clang-tidy-15" "clang-tidy"
        DOC "Path to clang-tidy executable"
    )
    if(NOT CLANG_TIDY_EXE)
        message(FATAL_ERROR
            "[MW] MW_CLANG_TIDY=ON but clang-tidy not found.\n"
            "     Install: apt-get install clang-tidy\n"
            "              or: yum install clang-tools-extra")
    endif()

    # .clang-tidy at project root configures check selection.
    # We pass --warnings-as-errors=* in CI via a separate preset.
    set(_clang_tidy_cmd
        "${CLANG_TIDY_EXE}"
        "--use-color"
        "--extra-arg=-Wno-unknown-warning-option"
    )
    if(MW_WARNINGS_AS_ERRORS)
        list(APPEND _clang_tidy_cmd "--warnings-as-errors=*")
    endif()

    set(CMAKE_C_CLANG_TIDY   "${_clang_tidy_cmd}")
    set(CMAKE_CXX_CLANG_TIDY "${_clang_tidy_cmd}")
    message(STATUS "[MW] Static analysis: clang-tidy enabled (${CLANG_TIDY_EXE})")
endif()

# ---------------------------------------------------------------------------
# cppcheck
# ---------------------------------------------------------------------------
if(MW_CPPCHECK)
    find_program(CPPCHECK_EXE NAMES "cppcheck" DOC "Path to cppcheck executable")
    if(NOT CPPCHECK_EXE)
        message(FATAL_ERROR
            "[MW] MW_CPPCHECK=ON but cppcheck not found.\n"
            "     Install: apt-get install cppcheck\n"
            "              or: yum install cppcheck")
    endif()

    set(CMAKE_C_CPPCHECK
        "${CPPCHECK_EXE}"
        "--enable=warning,performance,portability,missingInclude"
        "--suppress=missingIncludeSystem"
        "--inline-suppr"
        "--std=c17"
        "--error-exitcode=1"
        "--template=gcc"
    )
    message(STATUS "[MW] Static analysis: cppcheck enabled (${CPPCHECK_EXE})")
endif()

# ---------------------------------------------------------------------------
# include-what-you-use
# ---------------------------------------------------------------------------
if(MW_IWYU)
    find_program(IWYU_EXE NAMES "include-what-you-use" "iwyu"
        DOC "Path to include-what-you-use")
    if(NOT IWYU_EXE)
        message(FATAL_ERROR
            "[MW] MW_IWYU=ON but include-what-you-use not found.\n"
            "     See: https://github.com/include-what-you-use/include-what-you-use")
    endif()

    set(CMAKE_C_INCLUDE_WHAT_YOU_USE
        "${IWYU_EXE}"
        "-Xiwyu" "--no_fwd_decls"
        "-Xiwyu" "--cxx17ns"
    )
    set(CMAKE_CXX_INCLUDE_WHAT_YOU_USE "${CMAKE_C_INCLUDE_WHAT_YOU_USE}")
    message(STATUS "[MW] Static analysis: iwyu enabled (${IWYU_EXE})")
endif()
