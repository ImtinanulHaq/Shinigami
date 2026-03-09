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
    ${CMAKE_SOURCE_DIR}/../dev/hal/interface
    ${CMAKE_SOURCE_DIR}/../dev/security/core
)

target_link_libraries(gpio_service PRIVATE
    service_common
    ssl
    crypto
    pthread
)

install(TARGETS gpio_service DESTINATION sbin)
install(FILES gpio_service.conf  DESTINATION etc/platform-services)
install(FILES gpio_service.service
        DESTINATION lib/systemd/system)
