cmake_minimum_required(VERSION 3.16)
project(MiddlewareTesting C)

set(CMAKE_C_STANDARD 11)
set(WORKSPACE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

# ── Build flags (non-negotiable) ──────────────────────────────────────
set(TEST_COMPILE_FLAGS
    -Wall -Wextra -g -O0
    -fprofile-arcs -ftest-coverage
    -fsanitize=address,undefined
    -D_POSIX_C_SOURCE=200809L
    -D_GNU_SOURCE
    -DFUZZ_STANDALONE
)

set(TEST_LINK_FLAGS
    -fprofile-arcs
    -fsanitize=address,undefined
    -lpthread -lm
)

# ── Include paths ─────────────────────────────────────────────────────
set(INC
    ${WORKSPACE_ROOT}/dev/core
    ${WORKSPACE_ROOT}/dev/core/service_manager
    ${WORKSPACE_ROOT}/dev/core/service_manager/infrastructure
    ${WORKSPACE_ROOT}/dev/core/service_manager/security
    ${WORKSPACE_ROOT}/dev/core/service_manager/observability
    ${WORKSPACE_ROOT}/dev/core/service_manager/lifecycle
    ${WORKSPACE_ROOT}/dev/core/service_manager/enterprise
    ${WORKSPACE_ROOT}/dev/hal
    ${WORKSPACE_ROOT}/dev/hal/interface
    ${WORKSPACE_ROOT}/dev/hal/layers/audio
    ${WORKSPACE_ROOT}/dev/hal/layers/camera
    ${WORKSPACE_ROOT}/dev/hal/layers/sensors
    ${WORKSPACE_ROOT}/dev/hal/layers/gpio
    ${WORKSPACE_ROOT}/dev/security
    ${WORKSPACE_ROOT}/dev/security/core
    ${WORKSPACE_ROOT}/dev/security/capabilities
    ${WORKSPACE_ROOT}/dev/security/sandbox
    ${WORKSPACE_ROOT}/dev/security/seccomp
    ${WORKSPACE_ROOT}/dev/security/verify
    ${CMAKE_CURRENT_SOURCE_DIR}/framework
    ${CMAKE_CURRENT_SOURCE_DIR}/helpers
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks
)

# ── Unity library ─────────────────────────────────────────────────────
add_library(unity STATIC
    framework/unity.c
    framework/unity_fixture.c
)
target_include_directories(unity PUBLIC framework)
target_compile_options(unity PRIVATE -g -O0)

# ── Helpers library ───────────────────────────────────────────────────
add_library(test_helpers STATIC
    helpers/test_utils.c
    helpers/leak_detector.c
)
target_include_directories(test_helpers PUBLIC helpers framework)
target_compile_options(test_helpers PRIVATE ${TEST_COMPILE_FLAGS})

# ── Mocks library ─────────────────────────────────────────────────────
add_library(test_mocks STATIC
    mocks/mock_hardware.c
    mocks/mock_hal.c
    mocks/mock_sm.c
    mocks/mock_security.c
)
target_include_directories(test_mocks PUBLIC mocks helpers framework ${INC})
target_compile_options(test_mocks PRIVATE ${TEST_COMPILE_FLAGS})

# ── Production source libraries ───────────────────────────────────────

file(GLOB SM_INFRA_SRCS
    ${WORKSPACE_ROOT}/dev/core/service_manager/infrastructure/*.c)
file(GLOB SM_SECURITY_SRCS
    ${WORKSPACE_ROOT}/dev/core/service_manager/security/*.c)
file(GLOB SM_OBSERVABILITY_SRCS
    ${WORKSPACE_ROOT}/dev/core/service_manager/observability/*.c)
file(GLOB SM_LIFECYCLE_SRCS
    ${WORKSPACE_ROOT}/dev/core/service_manager/lifecycle/*.c)
file(GLOB SM_ENTERPRISE_SRCS
    ${WORKSPACE_ROOT}/dev/core/service_manager/enterprise/*.c)

file(GLOB SEC_CAP_SRCS     ${WORKSPACE_ROOT}/dev/security/capabilities/*.c)
file(GLOB SEC_SANDBOX_SRCS ${WORKSPACE_ROOT}/dev/security/sandbox/*.c)
file(GLOB SEC_SECCOMP_SRCS ${WORKSPACE_ROOT}/dev/security/seccomp/*.c)
file(GLOB SEC_VERIFY_SRCS  ${WORKSPACE_ROOT}/dev/security/verify/*.c)
file(GLOB SEC_CORE_SRCS    ${WORKSPACE_ROOT}/dev/security/core/*.c)

add_library(lib_sm STATIC
    ${SM_INFRA_SRCS}
    ${SM_SECURITY_SRCS}
    ${SM_OBSERVABILITY_SRCS}
    ${SM_LIFECYCLE_SRCS}
    ${SM_ENTERPRISE_SRCS}
)
target_include_directories(lib_sm PUBLIC ${INC})
target_compile_options(lib_sm PRIVATE
    -g -O0 -fprofile-arcs -ftest-coverage
    -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE
    -Wno-error
)

add_library(lib_ring_buffer STATIC
    ${WORKSPACE_ROOT}/dev/core/ring_buffer.c
)
target_include_directories(lib_ring_buffer PUBLIC ${WORKSPACE_ROOT}/dev/core)
target_compile_options(lib_ring_buffer PRIVATE
    -g -O0 -fprofile-arcs -ftest-coverage
    -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE)

add_library(lib_core STATIC
    ${WORKSPACE_ROOT}/dev/core/memory_pool.c
    ${WORKSPACE_ROOT}/dev/core/io_uring_loop.c
)
target_include_directories(lib_core PUBLIC ${WORKSPACE_ROOT}/dev/core)
target_compile_options(lib_core PRIVATE
    -g -O0 -fprofile-arcs -ftest-coverage
    -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE
    -Wno-error)

add_library(lib_hal STATIC
    ${WORKSPACE_ROOT}/dev/hal/interface/hal_interface.c
    ${WORKSPACE_ROOT}/dev/hal/layers/audio/audio_hal.c
    ${WORKSPACE_ROOT}/dev/hal/layers/camera/camera_hal.c
    ${WORKSPACE_ROOT}/dev/hal/layers/gpio/gpio_hal.c
    ${WORKSPACE_ROOT}/dev/hal/layers/sensors/sensor_hal.c
)
target_include_directories(lib_hal PUBLIC ${INC})
target_compile_options(lib_hal PRIVATE
    -g -O0 -fprofile-arcs -ftest-coverage
    -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE
    -Wno-error
)

add_library(lib_security STATIC
    ${SEC_CAP_SRCS}
    ${SEC_SANDBOX_SRCS}
    ${SEC_SECCOMP_SRCS}
    ${SEC_VERIFY_SRCS}
    ${SEC_CORE_SRCS}
)
target_include_directories(lib_security PUBLIC ${INC})
target_compile_options(lib_security PRIVATE
    -g -O0 -fprofile-arcs -ftest-coverage
    -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE
    -Wno-error
)

# ── Convenience macro: add a test executable ──────────────────────────
#   add_test_exe(<name> <sources...>)
macro(add_test_exe NAME)
    add_executable(${NAME} ${ARGN})
    target_include_directories(${NAME} PRIVATE ${INC}
        ${CMAKE_CURRENT_SOURCE_DIR}/framework
        ${CMAKE_CURRENT_SOURCE_DIR}/helpers
        ${CMAKE_CURRENT_SOURCE_DIR}/mocks
    )
    target_compile_options(${NAME} PRIVATE ${TEST_COMPILE_FLAGS})
    target_link_libraries(${NAME}
        unity test_mocks
        lib_ring_buffer lib_core lib_sm lib_hal lib_security
        test_helpers pthread m uring
    )
    target_link_options(${NAME} PRIVATE
        -fprofile-arcs
        -fsanitize=address,undefined
    )
    add_test(NAME ${NAME} COMMAND ${NAME})
endmacro()

# ── Unit tests ────────────────────────────────────────────────────────
enable_testing()

add_test_exe(test_ring_buffer       unit/test_ring_buffer.c)
add_test_exe(test_memory_pool       unit/test_memory_pool.c)
add_test_exe(test_io_uring_loop     unit/test_io_uring_loop.c)
add_test_exe(test_service_manager   unit/test_service_manager.c)
add_test_exe(test_hal_interface     unit/test_hal_interface.c)
add_test_exe(test_audio_hal         unit/test_audio_hal.c)
add_test_exe(test_camera_hal        unit/test_camera_hal.c)
add_test_exe(test_sensor_hal        unit/test_sensor_hal.c)
add_test_exe(test_gpio_hal          unit/test_gpio_hal.c)
add_test_exe(test_services          unit/test_services.c)

# ── Integration tests ─────────────────────────────────────────────────
add_test_exe(test_sm_hal_integration
    integration/test_sm_hal_integration.c)
add_test_exe(test_sm_services_integration
    integration/test_sm_services_integration.c)
add_test_exe(test_services_hal_integration
    integration/test_services_hal_integration.c)
add_test_exe(test_memory_pool_io_uring
    integration/test_memory_pool_io_uring.c)
add_test_exe(test_full_system
    integration/test_full_system.c)

# ── Stress tests (long-running — excluded from default ctest) ─────────
add_test_exe(stress_ring_buffer      stress/stress_ring_buffer.c)
add_test_exe(stress_memory_pool      stress/stress_memory_pool.c)
add_test_exe(stress_service_manager  stress/stress_service_manager.c)
add_test_exe(stress_full_system      stress/stress_full_system.c)

set_tests_properties(
    stress_ring_buffer stress_memory_pool
    stress_service_manager stress_full_system
    PROPERTIES LABELS "stress" TIMEOUT 300
)

# ── Fuzz tests (standalone mode) ──────────────────────────────────────
add_test_exe(fuzz_sm_protocol   fuzz/fuzz_sm_protocol.c)
add_test_exe(fuzz_hal_input     fuzz/fuzz_hal_input.c)
add_test_exe(fuzz_ipc_messages  fuzz/fuzz_ipc_messages.c)

set_tests_properties(
    fuzz_sm_protocol fuzz_hal_input fuzz_ipc_messages
    PROPERTIES LABELS "fuzz" TIMEOUT 120
)

# ── Coverage target (requires gcov + optional lcov) ───────────────────
find_program(GCOV_EXECUTABLE  gcov)
find_program(LCOV_EXECUTABLE  lcov)
find_program(GENHTML_EXECUTABLE genhtml)

if(GCOV_EXECUTABLE)
    add_custom_target(coverage
        COMMENT "Generating coverage report"
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/reports/coverage
        COMMAND ctest --label-exclude "stress|fuzz" -j4 || true
    )

    if(LCOV_EXECUTABLE AND GENHTML_EXECUTABLE)
        add_custom_command(TARGET coverage POST_BUILD
            COMMAND ${LCOV_EXECUTABLE} --gcov-tool ${GCOV_EXECUTABLE}
                    --capture --directory ${CMAKE_BINARY_DIR}
                    --output-file ${CMAKE_BINARY_DIR}/reports/coverage/coverage.info
                    --ignore-errors source
            COMMAND ${LCOV_EXECUTABLE}
                    --remove ${CMAKE_BINARY_DIR}/reports/coverage/coverage.info
                    '*/framework/*' '*/mocks/*' '*/helpers/*' '/usr/*'
                    --output-file ${CMAKE_BINARY_DIR}/reports/coverage/filtered.info
            COMMAND ${GENHTML_EXECUTABLE}
                    ${CMAKE_BINARY_DIR}/reports/coverage/filtered.info
                    --output-directory ${CMAKE_BINARY_DIR}/reports/coverage/html
            COMMAND ${CMAKE_COMMAND} -E echo
                    "Coverage HTML: ${CMAKE_BINARY_DIR}/reports/coverage/html/index.html"
        )
    else()
        add_custom_command(TARGET coverage POST_BUILD
            COMMAND find ${CMAKE_BINARY_DIR} -name "*.gcda" | head -5
            COMMAND ${CMAKE_COMMAND} -E echo
                    "lcov/genhtml not installed — raw .gcda files available"
        )
    endif()
endif()
