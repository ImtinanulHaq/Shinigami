# =============================================================================
# proxy/CMakeLists.txt
# Builds: libmiddleware_proxy.so  (shared)
#          libmiddleware_proxy.a   (static)
#          proxy unit/integration tests (GTest)
#          example binaries
# =============================================================================

cmake_minimum_required(VERSION 3.18)
project(middleware_proxy VERSION 1.0.0 LANGUAGES CXX)

# ── Language standard ─────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD          17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)

# ── Compile options ───────────────────────────────────────────────────────────
add_compile_options(
    -Wall -Wextra -Wpedantic -Werror
    -fvisibility=hidden
    -fstack-protector-strong
    -D_FORTIFY_SOURCE=2
)

# ── Find dependencies ─────────────────────────────────────────────────────────
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

# Optional: GTest for tests
find_package(GTest QUIET)

# ── Paths to the C middleware build artefacts ─────────────────────────────────
# Users can override these at cmake invocation:
#   cmake -DMIDDLEWARE_BUILD_DIR=<path> ..
set(MIDDLEWARE_BUILD_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../build/dev"
    CACHE PATH "Directory that contains the built C middleware static libs")

set(MIDDLEWARE_DEV_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../dev"
    CACHE PATH "Directory containing the C middleware source headers")

# ── Source files ──────────────────────────────────────────────────────────────

set(PROXY_BASE_SOURCES
    base/proxy_connection.cpp
    base/proxy_thread_pool.cpp
    base/proxy_shared_memory.cpp
    base/proxy_event_loop.cpp
    base/service_proxy.cpp
)

set(PROXY_SECURITY_SOURCES
    security/proxy_auth.cpp
)

set(PROXY_DISCOVERY_SOURCES
    discovery/service_discovery.cpp
)

set(PROXY_AUDIO_SOURCES
    audio/audio_shm_reader.cpp
    audio/audio_proxy.cpp
)

set(PROXY_CAMERA_SOURCES
    camera/camera_shm_reader.cpp
    camera/camera_proxy.cpp
)

set(PROXY_SENSOR_SOURCES
    sensor/sensor_proxy.cpp
)

set(PROXY_GPIO_SOURCES
    gpio/gpio_proxy.cpp
)

set(PROXY_BINDING_SOURCES
    bindings/c_api/proxy_c_api.cpp
)

set(PROXY_ALL_SOURCES
    ${PROXY_BASE_SOURCES}
    ${PROXY_SECURITY_SOURCES}
    ${PROXY_DISCOVERY_SOURCES}
    ${PROXY_AUDIO_SOURCES}
    ${PROXY_CAMERA_SOURCES}
    ${PROXY_SENSOR_SOURCES}
    ${PROXY_GPIO_SOURCES}
    ${PROXY_BINDING_SOURCES}
)

# ── Shared library ────────────────────────────────────────────────────────────
add_library(middleware_proxy_shared SHARED ${PROXY_ALL_SOURCES})

target_include_directories(middleware_proxy_shared
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${MIDDLEWARE_DEV_DIR}
        ${OPENSSL_INCLUDE_DIR}
)

target_link_libraries(middleware_proxy_shared
    PUBLIC
        Threads::Threads
    PRIVATE
        OpenSSL::SSL
        OpenSSL::Crypto
        # C middleware libraries (pre-built):
        ${MIDDLEWARE_BUILD_DIR}/core/service_manager/libservicemanager_common.a
        ${MIDDLEWARE_BUILD_DIR}/security/verify/libsecurity_verify.a
        # System:
        rt    # shm_open, shm_unlink
)

set_target_properties(middleware_proxy_shared PROPERTIES
    OUTPUT_NAME   middleware_proxy
    SOVERSION     1
    VERSION       ${PROJECT_VERSION}
    C_VISIBILITY_PRESET   hidden
    CXX_VISIBILITY_PRESET hidden
)

# ── Static library ────────────────────────────────────────────────────────────
add_library(middleware_proxy_static STATIC ${PROXY_ALL_SOURCES})

target_include_directories(middleware_proxy_static
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${MIDDLEWARE_DEV_DIR}
        ${OPENSSL_INCLUDE_DIR}
)

target_link_libraries(middleware_proxy_static
    PUBLIC  Threads::Threads
    PRIVATE OpenSSL::SSL OpenSSL::Crypto rt
)

set_target_properties(middleware_proxy_static PROPERTIES
    OUTPUT_NAME middleware_proxy)

# ── Alias targets (importable as middleware::proxy_shared etc.) ───────────────
add_library(middleware::proxy_shared ALIAS middleware_proxy_shared)
add_library(middleware::proxy_static ALIAS middleware_proxy_static)

# ── Tests ─────────────────────────────────────────────────────────────────────
if(GTest_FOUND OR TARGET GTest::gtest)
    enable_testing()

    set(MOCK_SOURCES
        testing/mocks/mock_service_daemon.cpp
    )

    function(add_proxy_test TARGET_NAME)
        cmake_parse_arguments(T "" "" "SOURCES" ${ARGN})
        add_executable(${TARGET_NAME} ${T_SOURCES} ${MOCK_SOURCES})
        target_include_directories(${TARGET_NAME} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/..
            ${MIDDLEWARE_DEV_DIR}
            ${OPENSSL_INCLUDE_DIR})
        target_link_libraries(${TARGET_NAME}
            PRIVATE
                middleware_proxy_static
                GTest::gtest
                GTest::gtest_main
                Threads::Threads
                OpenSSL::SSL OpenSSL::Crypto rt)
        add_test(NAME ${TARGET_NAME} COMMAND ${TARGET_NAME})
    endfunction()

    add_proxy_test(test_proxy_result
        SOURCES testing/unit/test_proxy_result.cpp)

    add_proxy_test(test_proxy_connection
        SOURCES testing/unit/test_proxy_connection.cpp)

    add_proxy_test(test_proxy_sm_integration
        SOURCES testing/integration/test_proxy_sm_integration.cpp)
endif()

# ── Examples ──────────────────────────────────────────────────────────────────
option(BUILD_PROXY_EXAMPLES "Build proxy example binaries" OFF)

if(BUILD_PROXY_EXAMPLES)
    foreach(EXAMPLE audio_capture camera_stream sensor_poll)
        add_executable(proxy_example_${EXAMPLE}
            examples/${EXAMPLE}_example.cpp)
        target_include_directories(proxy_example_${EXAMPLE} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/..
            ${MIDDLEWARE_DEV_DIR}
            ${OPENSSL_INCLUDE_DIR})
        target_link_libraries(proxy_example_${EXAMPLE}
            PRIVATE middleware_proxy_shared Threads::Threads rt)
    endforeach()
endif()

# ── Install ───────────────────────────────────────────────────────────────────
include(GNUInstallDirs)

install(TARGETS middleware_proxy_shared middleware_proxy_static
    EXPORT  MiddlewareProxyTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})

# Install public headers preserving directory structure.
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/middleware/proxy
    FILES_MATCHING
        PATTERN "*.h"
        PATTERN "testing/*" EXCLUDE
        PATTERN "examples/*" EXCLUDE)

install(EXPORT MiddlewareProxyTargets
    NAMESPACE   middleware::
    FILE        MiddlewareProxyTargets.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/middleware_proxy)

# ── Summary ───────────────────────────────────────────────────────────────────
message(STATUS "==========================================================")
message(STATUS "  middleware_proxy ${PROJECT_VERSION}")
message(STATUS "  Build dir:  ${CMAKE_BINARY_DIR}")
message(STATUS "  C libs dir: ${MIDDLEWARE_BUILD_DIR}")
message(STATUS "  OpenSSL:    ${OPENSSL_VERSION}")
message(STATUS "  GTest:      ${GTest_FOUND}")
message(STATUS "  Examples:   ${BUILD_PROXY_EXAMPLES}")
message(STATUS "==========================================================")
