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
    ${CMAKE_SOURCE_DIR}/../hal/interface
    ${CMAKE_SOURCE_DIR}/../security/core
)

# Link order: HAL → memory pool → security → service-common → crypto → pthreads
target_link_libraries(audio_service PRIVATE
    io_uring_loop_lib security_manager_lib sm_crypto_lib hal_audio_lib hal_interface_lib verify_lib
    memory_pool_lib ring_buffer_lib
    service_common
    ssl crypto pthread
)

# Install
install(TARGETS audio_service DESTINATION sbin)
install(FILES audio_service.conf  DESTINATION etc/platform-services)
install(FILES audio_service.service
        DESTINATION lib/systemd/system)
