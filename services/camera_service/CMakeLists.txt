##############################################################################
# camera_service/CMakeLists.txt
##############################################################################

cmake_minimum_required(VERSION 3.16)

set(CAMERA_SVC_SOURCES
    camera_service_main.c
    camera_service_hal.c
    camera_service_loop.c
    camera_service_security.c
)

add_executable(camera_service ${CAMERA_SVC_SOURCES})

target_include_directories(camera_service PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/common
    ${CMAKE_SOURCE_DIR}/../dev/hal/interface
    ${CMAKE_SOURCE_DIR}/../dev/security/core
)

target_link_libraries(camera_service PRIVATE
    service_common
    ssl
    crypto
    pthread
)

install(TARGETS camera_service DESTINATION sbin)
install(FILES camera_service.conf  DESTINATION etc/platform-services)
install(FILES camera_service.service
        DESTINATION lib/systemd/system)
