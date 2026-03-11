# =============================================================================
# dev/core/CMakeLists.txt
# Core primitives: io_uring event loop, memory pool, ring buffer.
#
# Produces: mw::core  (STATIC library — linked into every service daemon)
# =============================================================================

# ---------------------------------------------------------------------------
# mw_core — the three fundamental building blocks
# OBJECT library: compiled once, linked into each target separately.
# This avoids duplicating the .o files across multiple static archives.
# ---------------------------------------------------------------------------
add_library(mw_core STATIC)
add_library(mw::core ALIAS mw_core)

target_sources(mw_core
    PRIVATE
        io_uring_loop.c
        memory_pool.c
        ring_buffer.c
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
        FILES
            io_uring_loop.h
            memory_pool.h
            ring_buffer.h
)

target_include_directories(mw_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/mw/core>
)

target_link_libraries(mw_core
    PUBLIC
        Threads::Threads
        LibUring::LibUring
        mw::compile_opts
        mw::platform_checks
        mw::version
)

# PIE — all executables link mw_core; making it PIE-compatible is required
set_target_properties(mw_core PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    # Export symbols hidden by default — only explicitly marked symbols are exported
    C_VISIBILITY_PRESET hidden
)

# ---------------------------------------------------------------------------
# Service manager subtree
# The SM is decomposed into five sub-libraries that build on mw_core.
# Each sub-library is STATIC and not installed independently — they are
# merged into the sm_daemon executable and optionally exported as mw::sm_lib.
# ---------------------------------------------------------------------------
add_subdirectory(service_manager)

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
install(
    TARGETS mw_core
    EXPORT  linux_middleware_targets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
            COMPONENT   development
    FILE_SET HEADERS
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/mw/core"
    COMPONENT   development
)
