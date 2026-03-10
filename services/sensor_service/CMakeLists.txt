##############################################################################
# sensor_service/CMakeLists.txt
##############################################################################

cmake_minimum_required(VERSION 3.16)

set(SENSOR_SVC_SOURCES
    sensor_service_main.c
    sensor_service_hal.c
    sensor_service_loop.c
    sensor_service_security.c
)

add_executable(sensor_service ${SENSOR_SVC_SOURCES})

target_include_directories(sensor_service PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/common
    ${CMAKE_SOURCE_DIR}/../dev/hal/interface
    ${CMAKE_SOURCE_DIR}/../dev/security/core
)

target_link_libraries(sensor_service PRIVATE
    hal_sensor_lib hal_interface_lib verify_lib sm_crypto_lib ring_buffer_lib security_manager_lib
    io_uring_loop_lib memory_pool_lib
    service_common
    ssl crypto pthread
)

install(TARGETS sensor_service DESTINATION sbin)
install(FILES sensor_service.conf  DESTINATION etc/platform-services)
install(FILES sensor_service.service
        DESTINATION lib/systemd/system)
