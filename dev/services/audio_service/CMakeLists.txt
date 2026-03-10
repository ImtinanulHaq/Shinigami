# =============================================================================
# dev/services/audio_service/CMakeLists.txt
# Audio service daemon — ALSA PCM capture/playback over io_uring.
#
# Sources:
#   audio_service_main.c      daemon lifecycle, double-fork, CLI
#   audio_service_hal.c       ALSA device open/start/stop/read
#   audio_service_loop.c      io_uring: IO_OP_POLL on PCM fd + 1s timeout
#   audio_service_security.c  security_manager_default_config("audio")
#
# Link graph:
#   audio_service
#     ├── mw::svc_common   (service_base/config/ipc → mw::sm + mw::core)
#     ├── mw::hal_audio    (audio_hal.c → ALSA::ALSA)
#     ├── mw::security     (caps + sandbox + seccomp + verify)
#     ├── mw::core         (io_uring_loop, memory_pool, ring_buffer)
#     └── Threads::Threads
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
        # Own service headers (audio_service.h, audio_service_hal.h, etc.)
        ${CMAKE_CURRENT_SOURCE_DIR}
        # common/ headers included via "../common/" in source
        ${CMAKE_CURRENT_SOURCE_DIR}/../common
        # security_manager.h — directly included by audio_service_security.c
        # as "../../dev/security/core/security_manager.h"
        ${CMAKE_SOURCE_DIR}/dev/security/core
        ${CMAKE_SOURCE_DIR}/dev/security
        # Generated version header
        ${CMAKE_BINARY_DIR}/include
)

target_link_libraries(audio_service
    PRIVATE
        mw::svc_common      # service_base + config + ipc (carries mw::sm + mw::core)
        mw::hal_audio       # ALSA HAL (carries ALSA::ALSA transitively)
        mw::security        # full security stack (caps + sandbox + seccomp + verify)
        mw::core            # io_uring_loop, memory_pool, ring_buffer — explicit because
                            # audio_service_loop.c calls io_loop_* directly
        Threads::Threads
)

set_target_properties(audio_service PROPERTIES
    OUTPUT_NAME               "audio_service"
    POSITION_INDEPENDENT_CODE ON
    C_VISIBILITY_PRESET       hidden
)

target_compile_options(audio_service PRIVATE
    # Keep frame pointers — crash stacktraces from daemons are critical
    $<$<C_COMPILER_ID:GNU,Clang>:-fno-omit-frame-pointer>
)

target_link_options(audio_service PRIVATE
    "-Wl,--as-needed"       # strip unused DT_NEEDED entries
    "-Wl,-z,now"            # full RELRO: resolve all PLT symbols at load time
    "-Wl,-z,relro"          # make GOT read-only after relocation
    "-Wl,-z,noexecstack"    # non-executable stack segment
    "-Wl,--build-id=sha1"   # embed build ID for core-dump correlation
)

install(
    TARGETS audio_service
    RUNTIME
        DESTINATION "${CMAKE_INSTALL_SBINDIR}"
        COMPONENT   runtime
)
