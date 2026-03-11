# =============================================================================
# dev/security/CMakeLists.txt
# Security hardening stack: capabilities, sandbox, seccomp, verification.
#
# Produces:
#   mw::sec_capabilities — Linux capabilities management
#   mw::sec_sandbox      — Namespace + cgroup isolation
#   mw::sec_seccomp      — BPF filter generation
#   mw::sec_verify       — HMAC verification + token + replay protection
#   mw::security         — Aggregate: all four layers + SecurityManager
#
# Each sub-library is STATIC. Linked into each service daemon individually.
# mw::security is the single dependency for "full security" consumers.
# =============================================================================

# Shared include root — all security sub-packages include via relative paths
set(SEC_INCLUDE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")

# ---------------------------------------------------------------------------
# Helper macro
# ---------------------------------------------------------------------------
macro(mw_sec_sublibrary _target _src_dir)
    cmake_parse_arguments("_SEC" "" "" "SOURCES;LINK_LIBS" ${ARGN})

    add_library(${_target} STATIC)

    foreach(_src ${_SEC_SOURCES})
        target_sources(${_target} PRIVATE "${_src_dir}/${_src}")
    endforeach()

    target_include_directories(${_target}
        PUBLIC
            $<BUILD_INTERFACE:${SEC_INCLUDE_ROOT}>
            $<BUILD_INTERFACE:${SEC_INCLUDE_ROOT}/${_src_dir}>
            $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/include>
    )

    target_link_libraries(${_target}
        PUBLIC mw::compile_opts ${_SEC_LINK_LIBS}
    )

    set_target_properties(${_target} PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        C_VISIBILITY_PRESET hidden
    )
endmacro()

# ---------------------------------------------------------------------------
# 1. Capabilities — Linux capability management (drop/audit/policy)
# ---------------------------------------------------------------------------
mw_sec_sublibrary(mw_sec_capabilities "capabilities"
    SOURCES
        capabilities.c
        capabilities_core.c
        capabilities_policy.c
        capabilities_audit.c
    LINK_LIBS
        LibCap::LibCap
        mw::core
)
add_library(mw::sec_capabilities ALIAS mw_sec_capabilities)

if(NOT MW_SECURITY_CAPS)
    target_compile_definitions(mw_sec_capabilities
        PUBLIC MW_CAPS_STUB=1)
    message(STATUS "[MW] Security: capabilities STUBBED (MW_SECURITY_CAPS=OFF)")
endif()

# ---------------------------------------------------------------------------
# 2. Sandbox — namespace isolation, cgroups, mounts, network
# ---------------------------------------------------------------------------
mw_sec_sublibrary(mw_sec_sandbox "sandbox"
    SOURCES
        sandbox.c
        sandbox_core.c
        sandbox_cgroup.c
        sandbox_mount.c
        sandbox_network.c
    LINK_LIBS
        mw::core
)
add_library(mw::sec_sandbox ALIAS mw_sec_sandbox)

if(NOT MW_SECURITY_SANDBOX)
    target_compile_definitions(mw_sec_sandbox
        PUBLIC MW_SANDBOX_STUB=1)
    message(STATUS "[MW] Security: sandbox STUBBED (MW_SECURITY_SANDBOX=OFF)")
endif()

# ---------------------------------------------------------------------------
# 3. Seccomp — BPF filter generation + per-service syscall policies
# ---------------------------------------------------------------------------
mw_sec_sublibrary(mw_sec_seccomp "seccomp"
    SOURCES
        seccomp_core.c
        seccomp_filter.c
        seccomp_policy_minimal.c
        seccomp_policy_audio.c
        seccomp_policy_camera.c
        seccomp_policy_sensor.c
        seccomp_policy_network.c
    LINK_LIBS
        LibSeccomp::LibSeccomp
        mw::core
)
add_library(mw::sec_seccomp ALIAS mw_sec_seccomp)

if(NOT MW_SECURITY_SECCOMP)
    target_compile_definitions(mw_sec_seccomp
        PUBLIC MW_SECCOMP_STUB=1)
    message(STATUS "[MW] Security: seccomp STUBBED (MW_SECURITY_SECCOMP=OFF)")
endif()

# ---------------------------------------------------------------------------
# 4. Verification — HMAC, token issuance, replay protection
# ---------------------------------------------------------------------------
mw_sec_sublibrary(mw_sec_verify "verify"
    SOURCES
        verify.c
        verify_hmac.c
        verify_token.c
        verify_replay.c
    LINK_LIBS
        OpenSSL::Crypto
        mw::core
	mw::sm
)
add_library(mw::sec_verify ALIAS mw_sec_verify)

target_include_directories(mw_sec_verify PRIVATE ${CMAKE_SOURCE_DIR}/dev/core/service_manager/security)

if(NOT MW_SECURITY_VERIFY)
    target_compile_definitions(mw_sec_verify
        PUBLIC MW_VERIFY_STUB=1)
    message(STATUS "[MW] Security: verify STUBBED (MW_SECURITY_VERIFY=OFF)")
endif()

# ---------------------------------------------------------------------------
# 5. SecurityManager — orchestrates the above four layers
# ---------------------------------------------------------------------------
add_library(mw_sec_manager STATIC)
add_library(mw::sec_manager ALIAS mw_sec_manager)

target_sources(mw_sec_manager
    PRIVATE core/security_manager.c
)

target_include_directories(mw_sec_manager
    PUBLIC
        $<BUILD_INTERFACE:${SEC_INCLUDE_ROOT}/core>
        $<BUILD_INTERFACE:${SEC_INCLUDE_ROOT}>
        $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/mw/security>
)

target_link_libraries(mw_sec_manager
    PUBLIC
        mw_sec_capabilities
        mw_sec_sandbox
        mw_sec_seccomp
        mw_sec_verify
        mw::compile_opts
)

set_target_properties(mw_sec_manager PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

# ---------------------------------------------------------------------------
# mw::security — aggregate target for full security stack
# ---------------------------------------------------------------------------
add_library(mw_security INTERFACE)
add_library(mw::security ALIAS mw_security)

target_link_libraries(mw_security
    INTERFACE
        mw_sec_capabilities
        mw_sec_sandbox
        mw_sec_seccomp
        mw_sec_verify
        mw_sec_manager
)

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
install(
    TARGETS
        mw_sec_capabilities mw_sec_sandbox mw_sec_seccomp
        mw_sec_verify mw_sec_manager
    EXPORT  linux_middleware_targets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}" COMPONENT development
)

install(
    FILES
        core/security_manager.h
        core/sec_error.h
        verify/verify.h
        verify/verify_hmac.h
        verify/verify_token.h
        verify/verify_replay.h
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/mw/security"
    COMPONENT   development
)
