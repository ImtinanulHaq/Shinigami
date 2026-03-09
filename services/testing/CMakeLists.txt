##############################################################################
# testing/CMakeLists.txt — unit and integration test suite
#
# Requires: service_common (from parent), OpenSSL, pthreads.
##############################################################################

cmake_minimum_required(VERSION 3.16)

# ── strict compiler flags for all test targets ───────────────────────────
add_compile_options(
    -Wall
    -Wextra
    -Werror
    -Wshadow
    -Wformat=2
)

# ── include paths shared by every test ───────────────────────────────────
set(TEST_INCLUDES
    ${CMAKE_SOURCE_DIR}/common
    ${CMAKE_SOURCE_DIR}/audio_service
    ${CMAKE_SOURCE_DIR}/camera_service
    ${CMAKE_SOURCE_DIR}/sensor_service
    ${CMAKE_SOURCE_DIR}/gpio_service
    ${CMAKE_SOURCE_DIR}/../dev/hal/interface
    ${CMAKE_SOURCE_DIR}/../dev/security/core
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks
)

# ── mock libraries ────────────────────────────────────────────────────────

add_library(mock_hal STATIC
    mocks/mock_hal.c
)
target_include_directories(mock_hal PUBLIC mocks
    ${CMAKE_SOURCE_DIR}/../dev/hal/interface)

add_library(mock_sm STATIC
    mocks/mock_sm.c
)
target_include_directories(mock_sm PUBLIC mocks
    ${CMAKE_SOURCE_DIR}/common)
target_link_libraries(mock_sm PRIVATE pthread ssl crypto)

# ── helper macro: add a test executable and register it with CTest ────────
macro(add_svc_test name sources)
    add_executable(${name} ${sources})
    target_include_directories(${name} PRIVATE ${TEST_INCLUDES})
    target_link_libraries(${name} PRIVATE
        service_common mock_hal mock_sm verify_lib
        hal_interface_lib sm_crypto_lib ring_buffer_lib security_manager_lib
        ssl crypto pthread)
    add_test(NAME ${name} COMMAND ${name})
endmacro()

# ── unit tests ────────────────────────────────────────────────────────────

add_svc_test(test_service_base
    unit/test_service_base.c)

add_svc_test(test_service_ipc
    unit/test_service_ipc.c)

add_svc_test(test_audio_hal_layer
    "unit/test_audio_hal_layer.c;${CMAKE_SOURCE_DIR}/audio_service/audio_service_hal.c")
target_link_libraries(test_audio_hal_layer PRIVATE hal_audio_lib)

add_svc_test(test_camera_hal_layer
    "unit/test_camera_hal_layer.c;${CMAKE_SOURCE_DIR}/camera_service/camera_service_hal.c")
target_link_libraries(test_camera_hal_layer PRIVATE hal_camera_lib)

add_svc_test(test_sensor_hal_layer
    "unit/test_sensor_hal_layer.c;${CMAKE_SOURCE_DIR}/sensor_service/sensor_service_hal.c")
target_link_libraries(test_sensor_hal_layer PRIVATE hal_sensor_lib)

add_svc_test(test_gpio_hal_layer
    "unit/test_gpio_hal_layer.c;${CMAKE_SOURCE_DIR}/gpio_service/gpio_service_hal.c")
target_link_libraries(test_gpio_hal_layer PRIVATE hal_gpio_lib)

# ── integration tests ─────────────────────────────────────────────────────

add_svc_test(test_audio_full
    "integration/test_audio_full.c;${CMAKE_SOURCE_DIR}/audio_service/audio_service_hal.c")
target_link_libraries(test_audio_full PRIVATE hal_audio_lib)

add_svc_test(test_camera_full
    "integration/test_camera_full.c;${CMAKE_SOURCE_DIR}/camera_service/camera_service_hal.c")
target_link_libraries(test_camera_full PRIVATE hal_camera_lib)

add_svc_test(test_sensor_full
    "integration/test_sensor_full.c;${CMAKE_SOURCE_DIR}/sensor_service/sensor_service_hal.c")
target_link_libraries(test_sensor_full PRIVATE hal_sensor_lib)

add_svc_test(test_gpio_full
    "integration/test_gpio_full.c;${CMAKE_SOURCE_DIR}/gpio_service/gpio_service_hal.c")
target_link_libraries(test_gpio_full PRIVATE hal_gpio_lib)
