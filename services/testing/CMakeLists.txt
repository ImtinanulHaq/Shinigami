# ===========================================================================
# CMakeLists.txt — services/testing
# Build and register all unit and integration tests for the service layer.
# ===========================================================================
cmake_minimum_required(VERSION 3.16)
project(services_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Compiler flags
# ---------------------------------------------------------------------------
add_compile_options(
    -Wall -Wextra -Wpedantic -Wshadow
    -fsanitize=address,undefined
    -g
)
add_link_options(
    -fsanitize=address,undefined
)

# ---------------------------------------------------------------------------
# Find OpenSSL for HMAC support used by service_ipc
# ---------------------------------------------------------------------------
find_package(OpenSSL REQUIRED)

# ---------------------------------------------------------------------------
# Include paths for the parent workspace
# ---------------------------------------------------------------------------
set(ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../..")   # → middleware/
set(SVC_DIR  "${CMAKE_CURRENT_SOURCE_DIR}/..")       # → middleware/services/
set(HAL_INC  "${ROOT_DIR}/dev/hal/interface")
set(SEC_INC  "${ROOT_DIR}/dev/security/core"
             "${ROOT_DIR}/dev/security/sandbox"
             "${ROOT_DIR}/dev/security/capabilities"
             "${ROOT_DIR}/dev/security/seccomp"
             "${ROOT_DIR}/dev/security/verify")
set(SM_INC   "${ROOT_DIR}/dev/core/service_manager/infrastructure")

include_directories(
    "${SVC_DIR}"
    "${SVC_DIR}/common"
    "${HAL_INC}"
    ${SEC_INC}
    "${SM_INC}"
)

# ---------------------------------------------------------------------------
# Mock libraries
# ---------------------------------------------------------------------------
add_library(mock_hal STATIC
    mocks/mock_hal.c
)
target_include_directories(mock_hal PUBLIC mocks "${HAL_INC}")

add_library(mock_sm STATIC
    mocks/mock_sm.c
)
target_include_directories(mock_sm PUBLIC mocks "${SM_INC}")
target_link_libraries(mock_sm PRIVATE pthread OpenSSL::Crypto)

# ---------------------------------------------------------------------------
# Common service library (shared by all tests)
# ---------------------------------------------------------------------------
add_library(service_common STATIC
    "${SVC_DIR}/common/service_base.c"
    "${SVC_DIR}/common/service_ipc.c"
    "${SVC_DIR}/common/service_config.c"
)
target_include_directories(service_common PUBLIC "${SVC_DIR}/common" "${SM_INC}")
target_link_libraries(service_common PUBLIC OpenSSL::Crypto)

# ---------------------------------------------------------------------------
# Per-service HAL+security layer libraries
# ---------------------------------------------------------------------------
add_library(svc_audio_lib STATIC
    "${SVC_DIR}/audio_service/audio_service_hal.c"
    "${SVC_DIR}/audio_service/audio_service_security.c"
    "${SVC_DIR}/audio_service/audio_service_loop.c"
)
target_include_directories(svc_audio_lib PUBLIC
    "${SVC_DIR}/audio_service" "${HAL_INC}" ${SEC_INC})
target_link_libraries(svc_audio_lib PUBLIC service_common)

add_library(svc_camera_lib STATIC
    "${SVC_DIR}/camera_service/camera_service_hal.c"
    "${SVC_DIR}/camera_service/camera_service_security.c"
    "${SVC_DIR}/camera_service/camera_service_loop.c"
)
target_include_directories(svc_camera_lib PUBLIC
    "${SVC_DIR}/camera_service" "${HAL_INC}" ${SEC_INC})
target_link_libraries(svc_camera_lib PUBLIC service_common)

add_library(svc_sensor_lib STATIC
    "${SVC_DIR}/sensor_service/sensor_service_hal.c"
    "${SVC_DIR}/sensor_service/sensor_service_security.c"
    "${SVC_DIR}/sensor_service/sensor_service_loop.c"
)
target_include_directories(svc_sensor_lib PUBLIC
    "${SVC_DIR}/sensor_service" "${HAL_INC}" ${SEC_INC})
target_link_libraries(svc_sensor_lib PUBLIC service_common)

add_library(svc_gpio_lib STATIC
    "${SVC_DIR}/gpio_service/gpio_service_hal.c"
    "${SVC_DIR}/gpio_service/gpio_service_security.c"
    "${SVC_DIR}/gpio_service/gpio_service_loop.c"
)
target_include_directories(svc_gpio_lib PUBLIC
    "${SVC_DIR}/gpio_service" "${HAL_INC}" ${SEC_INC})
target_link_libraries(svc_gpio_lib PUBLIC service_common)

# ---------------------------------------------------------------------------
# Enable CTest
# ---------------------------------------------------------------------------
enable_testing()

# ---------------------------------------------------------------------------
# Helper macro
# ---------------------------------------------------------------------------
macro(add_service_test test_name src_file)
    add_executable(${test_name} ${src_file})
    target_link_libraries(${test_name} PRIVATE ${ARGN})
    add_test(NAME ${test_name} COMMAND ${test_name})
    set_tests_properties(${test_name} PROPERTIES TIMEOUT 30)
endmacro()

# ---------------------------------------------------------------------------
# Unit tests
# ---------------------------------------------------------------------------
add_service_test(test_service_base
    unit/test_service_base.c
    service_common
)

add_service_test(test_service_ipc
    unit/test_service_ipc.c
    service_common mock_sm pthread
)

add_service_test(test_audio_hal_layer
    unit/test_audio_hal_layer.c
    svc_audio_lib mock_hal
)

add_service_test(test_camera_hal_layer
    unit/test_camera_hal_layer.c
    svc_camera_lib mock_hal
)

add_service_test(test_sensor_hal_layer
    unit/test_sensor_hal_layer.c
    svc_sensor_lib mock_hal
)

add_service_test(test_gpio_hal_layer
    unit/test_gpio_hal_layer.c
    svc_gpio_lib mock_hal
)

# ---------------------------------------------------------------------------
# Integration tests
# ---------------------------------------------------------------------------
add_service_test(test_audio_full
    integration/test_audio_full.c
    svc_audio_lib mock_sm mock_hal pthread
)

add_service_test(test_camera_full
    integration/test_camera_full.c
    svc_camera_lib mock_sm mock_hal pthread
)

add_service_test(test_sensor_full
    integration/test_sensor_full.c
    svc_sensor_lib mock_sm mock_hal pthread
)

add_service_test(test_gpio_full
    integration/test_gpio_full.c
    svc_gpio_lib mock_sm mock_hal pthread
)

# ---------------------------------------------------------------------------
# Custom target: run all tests with verbose output
# ---------------------------------------------------------------------------
add_custom_target(run_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -V
    DEPENDS
        test_service_base test_service_ipc
        test_audio_hal_layer test_camera_hal_layer
        test_sensor_hal_layer test_gpio_hal_layer
        test_audio_full test_camera_full
        test_sensor_full test_gpio_full
    COMMENT "Running service layer test suite..."
)
