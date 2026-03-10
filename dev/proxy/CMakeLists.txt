# =============================================================================
# dev/proxy/CMakeLists.txt
# C++ proxy library — high-level client-side API for application code.
#
# Produces:
#   mw_proxy   — SHARED library (versioned .so) — the main deliverable
#   mw_proxy_c — INTERFACE dependency alias exposing C bindings
#
# Design:
#   - Shared library (not static) so the C++ ABI is isolated from callers.
#   - All symbols hidden by default; only PROXY_API-annotated symbols exported.
#   - generate_export_header() produces mw_proxy_export.h — no manual macros.
#   - C bindings (proxy_c_api.cpp) link against mw_proxy and expose a stable
#     C ABI for services and embedded callers that can't consume C++ directly.
#
# If MW_BUILD_PROXY=OFF, this entire file is a no-op (via parent CMakeLists).
# =============================================================================

if(NOT MW_BUILD_PROXY)
    message(STATUS "[MW] Proxy: skipped (MW_BUILD_PROXY=OFF)")
    return()
endif()

# ---------------------------------------------------------------------------
# Proxy internal sub-libraries (OBJECT — compiled once, linked into mw_proxy)
# Using OBJECT avoids recompiling files that are shared across sub-dirs.
# ---------------------------------------------------------------------------

# Base infrastructure
add_library(mw_proxy_base OBJECT)
target_sources(mw_proxy_base
    PRIVATE
        base/proxy_connection.cpp
        base/proxy_event_loop.cpp
        base/proxy_shared_memory.cpp
        base/proxy_thread_pool.cpp
        base/service_proxy.cpp
)
target_include_directories(mw_proxy_base
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/base>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/include>
	$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/dev>
)
target_link_libraries(mw_proxy_base
    PUBLIC
        mw::compile_opts
        mw::core
        OpenSSL::Crypto
        Threads::Threads
)
set_target_properties(mw_proxy_base PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
)

# Security / auth
add_library(mw_proxy_auth OBJECT)
target_sources(mw_proxy_auth PRIVATE security/proxy_auth.cpp)
target_include_directories(mw_proxy_auth
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/security>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/base>
)
target_link_libraries(mw_proxy_auth PUBLIC mw_proxy_base OpenSSL::Crypto mw::sec_verify)
set_target_properties(mw_proxy_auth PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Service discovery (queries SM registry)
add_library(mw_proxy_discovery OBJECT)
target_sources(mw_proxy_discovery PRIVATE discovery/service_discovery.cpp)
target_include_directories(mw_proxy_discovery
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/discovery>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/base>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/dev/core/service_manager/infrastructure>
)
target_link_libraries(mw_proxy_discovery PUBLIC mw_proxy_base mw::sm)
set_target_properties(mw_proxy_discovery PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Per-device proxy implementations
foreach(_dev audio camera gpio sensor)
    string(TOUPPER "${_dev}" _DEV)

    add_library(mw_proxy_${_dev} OBJECT)

    # Source files vary slightly by device
    if(_dev STREQUAL "audio")
        target_sources(mw_proxy_${_dev} PRIVATE
            audio/audio_proxy.cpp
            audio/audio_shm_reader.cpp)
    elseif(_dev STREQUAL "camera")
        target_sources(mw_proxy_${_dev} PRIVATE
            camera/camera_proxy.cpp
            camera/camera_shm_reader.cpp)
    elseif(_dev STREQUAL "gpio")
        target_sources(mw_proxy_${_dev} PRIVATE gpio/gpio_proxy.cpp)
    elseif(_dev STREQUAL "sensor")
        target_sources(mw_proxy_${_dev} PRIVATE sensor/sensor_proxy.cpp)
    endif()

    target_include_directories(mw_proxy_${_dev}
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/${_dev}>
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/base>
    )
    target_link_libraries(mw_proxy_${_dev}
        PUBLIC mw_proxy_base mw_proxy_auth mw_proxy_discovery)
    set_target_properties(mw_proxy_${_dev} PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        CXX_VISIBILITY_PRESET hidden
    )
endforeach()

# ---------------------------------------------------------------------------
# mw_proxy — the versioned shared library
# All OBJECT sub-libraries are folded in here.
# ---------------------------------------------------------------------------
add_library(mw_proxy SHARED)
add_library(mw::proxy ALIAS mw_proxy)

# No sources of its own — pulls in all OBJECT sub-libraries
target_link_libraries(mw_proxy
    PRIVATE
        mw_proxy_base
        mw_proxy_auth
        mw_proxy_discovery
        mw_proxy_audio
        mw_proxy_camera
        mw_proxy_gpio
        mw_proxy_sensor
        mw::core
	mw::sm
        OpenSSL::Crypto
	mw::sec_verify
        Threads::Threads
        ${CMAKE_DL_LIBS}
)

# Public interface — only what API consumers need
target_include_directories(mw_proxy
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/base>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/audio>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/camera>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/gpio>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/sensor>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/discovery>
        $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/mw/proxy>
)

# ABI versioning: libmw_proxy.so.MAJOR
set_target_properties(mw_proxy PROPERTIES
    VERSION              "${PROJECT_VERSION}"
    SOVERSION            "${PROJECT_VERSION_MAJOR}"
    OUTPUT_NAME          "mw_proxy"
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

# Generate export macro header — replaces hand-written PROXY_API defines
generate_export_header(mw_proxy
    BASE_NAME            MW_PROXY
    EXPORT_MACRO_NAME    MW_PROXY_API
    EXPORT_FILE_NAME     "${CMAKE_BINARY_DIR}/include/mw/proxy/mw_proxy_export.h"
)

target_include_directories(mw_proxy
    PUBLIC $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/include/mw/proxy>
)

target_link_options(mw_proxy PRIVATE
    "-Wl,--as-needed"
    "-Wl,-z,now"
    "-Wl,-z,relro"
    "-Wl,-z,noexecstack"
)

# ---------------------------------------------------------------------------
# C bindings — thin C API shim over mw_proxy
# Compiled as a separate SHARED lib so C callers don't need a C++ runtime.
# ---------------------------------------------------------------------------
add_library(mw_proxy_c SHARED)
add_library(mw::proxy_c ALIAS mw_proxy_c)

target_sources(mw_proxy_c PRIVATE bindings/c_api/proxy_c_api.cpp)

target_include_directories(mw_proxy_c
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/bindings/c_api>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/mw/proxy>
)

target_link_libraries(mw_proxy_c
    PRIVATE mw::proxy
    PUBLIC  mw::compile_opts
)

set_target_properties(mw_proxy_c PROPERTIES
    VERSION   "${PROJECT_VERSION}"
    SOVERSION "${PROJECT_VERSION_MAJOR}"
    OUTPUT_NAME "mw_proxy_c"
    CXX_VISIBILITY_PRESET   hidden
    VISIBILITY_INLINES_HIDDEN ON
)

# ---------------------------------------------------------------------------
# Examples (optional — off by default)
# ---------------------------------------------------------------------------
if(MW_BUILD_EXAMPLES)
    foreach(_ex audio_capture camera_stream sensor_poll)
        add_executable(example_${_ex})
        target_sources(example_${_ex}
            PRIVATE examples/${_ex}_example.cpp)
        target_link_libraries(example_${_ex}
            PRIVATE mw::proxy)
        set_target_properties(example_${_ex}
            PROPERTIES OUTPUT_NAME "${_ex}_example")
    endforeach()
endif()

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
install(
    TARGETS mw_proxy mw_proxy_c
    EXPORT  linux_middleware_targets
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"   COMPONENT runtime
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"   COMPONENT runtime
            NAMELINK_COMPONENT development
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"   COMPONENT development
)

install(
    FILES
        base/proxy_config.h
        base/proxy_result.h
        base/service_proxy.h
        audio/audio_proxy.h
        audio/audio_frame.h
        audio/audio_shm_reader.h
        camera/camera_proxy.h
        camera/camera_frame.h
        camera/camera_shm_reader.h
        gpio/gpio_proxy.h
        sensor/sensor_proxy.h
        sensor/sensor_reading.h
        discovery/service_discovery.h
        bindings/c_api/proxy_c_api.h
        "${CMAKE_BINARY_DIR}/include/mw/proxy/mw_proxy_export.h"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/mw/proxy"
    COMPONENT   development
)
