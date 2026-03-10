# =============================================================================
# dev/services/audio_service/CMakeLists.txt
# Audio service daemon — ALSA PCM capture/playback.
#
# Links: mw::svc_common + mw::hal_audio + mw::security + mw::sm
# =============================================================================

add_executable(audio_service)

target_sources(audio_service
    PRIVATE
        audio_service_main.c
        audio_service_hal.c
        audio_service_loop.c
        audio_service_security.c
)

target_include_directories(audio_service
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/dev/services/common  # service_base.h etc.
)

target_link_libraries(audio_service
    PRIVATE
        mw::svc_common
        mw::hal_audio        # ALSA HAL layer
        mw::security         # full security stack
        mw::core             # io_uring, memory_pool, ring_buffer
        Threads::Threads
)

set_target_properties(audio_service PROPERTIES
    OUTPUT_NAME  "audio_service"
    POSITION_INDEPENDENT_CODE ON
)

target_link_options(audio_service PRIVATE
    "-Wl,--as-needed"
    "-Wl,-z,now"
    "-Wl,-z,relro"
    "-Wl,-z,noexecstack"
)

install(
    TARGETS audio_service
    RUNTIME DESTINATION "${CMAKE_INSTALL_SBINDIR}" COMPONENT runtime
)
