##############################################################################
# audio_service/CMakeLists.txt
##############################################################################

cmake_minimum_required(VERSION 3.16)

# Sources specific to this service
set(AUDIO_SVC_SOURCES
    audio_service_main.c
    audio_service_hal.c
    audio_service_loop.c
    audio_service_security.c
)

add_executable(audio_service ${AUDIO_SVC_SOURCES})

target_include_directories(audio_service PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/common
    ${CMAKE_SOURCE_DIR}/../dev/hal/interface
    ${CMAKE_SOURCE_DIR}/../dev/security/core
)

# Link order: HAL → security → service-common → crypto → pthreads
target_link_libraries(audio_service PRIVATE
    service_common
    ssl
    crypto
    pthread
)

# Install
install(TARGETS audio_service DESTINATION sbin)
install(FILES audio_service.conf  DESTINATION etc/platform-services)
install(FILES audio_service.service
        DESTINATION lib/systemd/system)
