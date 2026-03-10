##############################################################################
# gpio_service/CMakeLists.txt
##############################################################################

cmake_minimum_required(VERSION 3.16)

set(GPIO_SVC_SOURCES
    gpio_service_main.c
    gpio_service_hal.c
    gpio_service_loop.c
    gpio_service_security.c
)

add_executable(gpio_service ${GPIO_SVC_SOURCES})

target_include_directories(gpio_service PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/common
    ${CMAKE_SOURCE_DIR}/../hal/interface
    ${CMAKE_SOURCE_DIR}/../security/core
)

target_link_libraries(gpio_service PRIVATE
    io_uring_loop_lib security_manager_lib sm_crypto_lib hal_gpio_lib hal_interface_lib verify_lib
    memory_pool_lib ring_buffer_lib
    service_common
    ssl crypto pthread
)

install(TARGETS gpio_service DESTINATION sbin)
install(FILES gpio_service.conf  DESTINATION etc/platform-services)
install(FILES gpio_service.service
        DESTINATION lib/systemd/system)
