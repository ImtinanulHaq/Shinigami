# =============================================================================
# dev/core/service_manager/CMakeLists.txt
# Service Manager — sub-library decomposition.
#
# The SM is broken into cohesive sub-libraries by responsibility domain.
# Each domain is a STATIC OBJECT library — they are linked together into
# the sm_daemon executable and into mw_sm_lib (for the SM client API).
#
# Dependency graph (leaves at top):
#
#   mw_sm_infrastructure ──→ mw::core
#   mw_sm_security       ──→ mw::core, OpenSSL::Crypto
#   mw_sm_lifecycle      ──→ mw_sm_infrastructure, mw_sm_security
#   mw_sm_observability  ──→ mw_sm_infrastructure, mw_sm_lifecycle
#   mw_sm_enterprise     ──→ mw_sm_lifecycle, mw_sm_observability, mw::hal
#   mw_sm_lib            ──→ (all of the above) — the full SM as a linkable lib
#   sm_daemon            ──→ mw_sm_lib + mw::security
# =============================================================================

# Root include path for all SM sub-packages
# Sub-packages include each other via relative paths (e.g. "../lifecycle/sm_config.h")
# so we expose the service_manager/ dir as an include root.
set(SM_INCLUDE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")

# ---------------------------------------------------------------------------
# Helper macro — used by each sub-package
# mw_sm_sublibrary(<name> SOURCES src1 src2 ... HEADERS hdr1 hdr2 ...)
# ---------------------------------------------------------------------------
macro(mw_sm_sublibrary _lib_name)
    cmake_parse_arguments("_SL" "" "" "SOURCES;HEADERS;LINK_LIBS" ${ARGN})

    add_library(${_lib_name} STATIC)

    target_sources(${_lib_name}
        PRIVATE ${_SL_SOURCES}
    )

    target_include_directories(${_lib_name}
        PUBLIC
            $<BUILD_INTERFACE:${SM_INCLUDE_ROOT}>
            $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/include>
    )

    target_link_libraries(${_lib_name}
        PUBLIC
            mw::compile_opts
            ${_SL_LINK_LIBS}
    )

    set_target_properties(${_lib_name} PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        C_VISIBILITY_PRESET hidden
    )
endmacro()

# ---------------------------------------------------------------------------
# 1. Infrastructure — protocol, registry, socket, connection pool, handlers
# ---------------------------------------------------------------------------
mw_sm_sublibrary(mw_sm_infrastructure
    SOURCES
        infrastructure/sm_protocol.c
        infrastructure/sm_registry.c
        infrastructure/sm_socket.c
        infrastructure/sm_connection_pool.c
        infrastructure/sm_handlers.c
        infrastructure/sm_request_id.c
    LINK_LIBS
        mw::core
        OpenSSL::Crypto
        Threads::Threads
)

# ---------------------------------------------------------------------------
# 2. SM Security — crypto, rate limiting, HMAC key management
# ---------------------------------------------------------------------------
mw_sm_sublibrary(mw_sm_security
    SOURCES
        security/sm_crypto.c
        security/sm_rate_limit.c
        security/sm_advanced_ratelimit.c
        security/sm_security.c
    LINK_LIBS
        mw::core
        OpenSSL::Crypto
        mw_sm_infrastructure
)

# ---------------------------------------------------------------------------
# 3. Lifecycle — config, dependencies, shutdown, management, persistence, tiers
# ---------------------------------------------------------------------------
mw_sm_sublibrary(mw_sm_lifecycle
    SOURCES
        lifecycle/sm_config.c
        lifecycle/sm_dependencies.c
        lifecycle/sm_graceful_shutdown.c
        lifecycle/sm_main.c
        lifecycle/sm_management.c
        lifecycle/sm_persistence.c
        lifecycle/sm_service_tier.c
    LINK_LIBS
        mw_sm_infrastructure
        mw_sm_security
)

# ---------------------------------------------------------------------------
# 4. Observability — health, metrics, audit, logging, structured log
# ---------------------------------------------------------------------------
mw_sm_sublibrary(mw_sm_observability
    SOURCES
        observability/sm_health.c
        observability/sm_health_callbacks.c
        observability/sm_metrics.c
        observability/sm_audit.c
        observability/sm_logging.c
        observability/sm_structured_log.c
    LINK_LIBS
        mw_sm_infrastructure
        mw_sm_lifecycle
)

# ---------------------------------------------------------------------------
# 5. Enterprise — threadpool, TLS, watchdog, rolling restart, plugin, eventbus,
#                 discovery, container, CLI, main integration
# ---------------------------------------------------------------------------
mw_sm_sublibrary(mw_sm_enterprise
    SOURCES
        enterprise/sm_threadpool.c
        enterprise/sm_tls.c
        enterprise/sm_watchdog.c
        enterprise/sm_rolling_restart.c
        enterprise/sm_plugin.c
        enterprise/sm_eventbus.c
        enterprise/sm_discovery.c
        enterprise/sm_container.c
        enterprise/sm_cli.c
        enterprise/sm_main_integration.c
    LINK_LIBS
        mw_sm_lifecycle
        mw_sm_observability
        OpenSSL::SSL     # TLS layer
        OpenSSL::Crypto
        Threads::Threads
)

# ---------------------------------------------------------------------------
# mw_sm_lib — aggregate target: all SM sub-libraries in one linkable unit.
# This is what sm_daemon and tests link against.
# ---------------------------------------------------------------------------
add_library(mw_sm_lib STATIC)
add_library(mw::sm ALIAS mw_sm_lib)

# No sources of its own — it's a link aggregator
target_link_libraries(mw_sm_lib
    PUBLIC
        mw_sm_infrastructure
        mw_sm_security
        mw_sm_lifecycle
        mw_sm_observability
        mw_sm_enterprise
        mw::core
)

set_target_properties(mw_sm_lib PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

# ---------------------------------------------------------------------------
# sm_daemon — the service manager executable
# ---------------------------------------------------------------------------
add_executable(sm_daemon)

target_sources(sm_daemon
    PRIVATE
        lifecycle/sm_main.c    # contains main() for the server binary
)

target_link_libraries(sm_daemon
    PRIVATE
        mw::sm
        mw::security          # full security stack (set up after mw::security is built)
        Threads::Threads
        ${CMAKE_DL_LIBS}      # dlopen for plugin system
)

set_target_properties(sm_daemon PROPERTIES
    OUTPUT_NAME   "sm_daemon"
    PIE           TRUE         # Position-independent executable (ASLR)
    POSITION_INDEPENDENT_CODE ON
)

# Hardened executable — no need to link shared libs it doesn't use
target_link_options(sm_daemon PRIVATE
    "-Wl,--as-needed"
    "-Wl,-z,now"
    "-Wl,-z,relro"
    "-Wl,-z,noexecstack"
)

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
install(
    TARGETS sm_daemon
    RUNTIME DESTINATION "${CMAKE_INSTALL_SBINDIR}"
            COMPONENT   runtime
)

install(
    TARGETS mw_sm_lib mw_sm_infrastructure mw_sm_security
            mw_sm_lifecycle mw_sm_observability mw_sm_enterprise
    EXPORT  linux_middleware_targets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
            COMPONENT   development
)

# Public SM client API header — services include this to register with the SM
install(
    FILES
        lifecycle/sm_main.h
        infrastructure/sm_protocol.h
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/mw/sm"
    COMPONENT   development
)
