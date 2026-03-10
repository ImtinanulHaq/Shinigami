# =============================================================================
# dev/hal/CMakeLists.txt
# Hardware Abstraction Layer — common interface + per-device implementations.
#
# Produces:
#   mw::hal_iface   — INTERFACE library: hal_interface.h only, no .c
#   mw::hal_audio   — STATIC: audio_hal.c   (requires ALSA)
#   mw::hal_camera  — STATIC: camera_hal.c  (requires V4L2 headers)
#   mw::hal_gpio    — STATIC: gpio_hal.c
#   mw::hal_sensors — STATIC: sensor_hal.c
#   mw::hal         — STATIC aggregate: all four layers + interface
#
# Each layer can be conditionally excluded via MW_HAL_* options.
# The mw::hal aggregate always exists but may be empty if all layers are off.
# =============================================================================

# ---------------------------------------------------------------------------
# HAL interface — header-only, no sources
# Every HAL consumer links only this to get hal_interface.h
# ---------------------------------------------------------------------------
# ---------------------------------------------------------------------------
# HAL interface — common registry / lifecycle helpers
# ---------------------------------------------------------------------------
add_library(mw_hal_iface STATIC)           # <--- CHANGE TO STATIC
add_library(mw::hal_iface ALIAS mw_hal_iface)

target_sources(mw_hal_iface
    PRIVATE                                # <--- ADD THIS BLOCK
        interface/hal_interface.c          # <--- ADD THIS LINE
    PUBLIC                                 # <--- CHANGE TO PUBLIC (was INTERFACE)
        FILE_SET HEADERS
        BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/interface"
        FILES
            interface/hal_interface.h
)

target_include_directories(mw_hal_iface
    PUBLIC                                 # <--- CHANGE TO PUBLIC (was INTERFACE)
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/interface>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/mw/hal>
)

target_link_libraries(mw_hal_iface
    PUBLIC                                 # <--- CHANGE TO PUBLIC (was INTERFACE)
        mw::compile_opts
        mw::version
        Threads::Threads
)

set_target_properties(mw_hal_iface PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------------------
# Helper macro — avoids repeating the same boilerplate for each HAL layer
# ---------------------------------------------------------------------------
macro(mw_hal_layer _target_suffix _src_dir _sources)
    set(_tgt "mw_hal_${_target_suffix}")
    add_library(${_tgt} STATIC)
    add_library("mw::hal_${_target_suffix}" ALIAS ${_tgt})

    target_sources(${_tgt} PRIVATE ${_sources})

    target_include_directories(${_tgt}
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/${_src_dir}>
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/interface>
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/${_src_dir}
    )

    target_link_libraries(${_tgt}
        PUBLIC
            mw::hal_iface
            mw::compile_opts
            mw::core
    )

    set_target_properties(${_tgt} PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        C_VISIBILITY_PRESET hidden
    )
endmacro()

# ---------------------------------------------------------------------------
# Audio HAL — ALSA PCM backend
# ---------------------------------------------------------------------------
if(MW_HAL_AUDIO)
    mw_hal_layer(audio layers/audio "layers/audio/audio_hal.c")
    target_link_libraries(mw_hal_audio PUBLIC ALSA::ALSA)
    target_compile_definitions(mw_hal_audio PUBLIC MW_HAL_AUDIO=1)
    message(STATUS "[MW] HAL: audio enabled")
endif()

# ---------------------------------------------------------------------------
# Camera HAL — V4L2 backend
# ---------------------------------------------------------------------------
if(MW_HAL_CAMERA)
    mw_hal_layer(camera layers/camera "layers/camera/camera_hal.c")
    # V4L2 is kernel headers only — no library to link
    target_compile_definitions(mw_hal_camera PUBLIC MW_HAL_CAMERA=1)
    message(STATUS "[MW] HAL: camera enabled")
endif()

# ---------------------------------------------------------------------------
# GPIO HAL — sysfs/gpiod backend
# ---------------------------------------------------------------------------
if(MW_HAL_GPIO)
    mw_hal_layer(gpio layers/gpio "layers/gpio/gpio_hal.c")
    target_compile_definitions(mw_hal_gpio PUBLIC MW_HAL_GPIO=1)
    message(STATUS "[MW] HAL: gpio enabled")
endif()

# ---------------------------------------------------------------------------
# Sensor HAL — IIO backend
# ---------------------------------------------------------------------------
if(MW_HAL_SENSORS)
    mw_hal_layer(sensors layers/sensors "layers/sensors/sensor_hal.c")
    target_compile_definitions(mw_hal_sensors PUBLIC MW_HAL_SENSORS=1)
    message(STATUS "[MW] HAL: sensors enabled")
endif()

# ---------------------------------------------------------------------------
# mw_hal — aggregate target.
# Links all enabled layers.
# Consumers link mw::hal and get everything that's compiled in.
# ---------------------------------------------------------------------------
add_library(mw_hal INTERFACE)               # <--- CHANGE TO INTERFACE
add_library(mw::hal ALIAS mw_hal)

target_include_directories(mw_hal
    INTERFACE                               # <--- CHANGE TO INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/interface>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/mw/hal>
)

target_link_libraries(mw_hal
    INTERFACE                               # <--- CHANGE TO INTERFACE
        mw::hal_iface
        mw::core
        $<$<BOOL:${MW_HAL_AUDIO}>:   mw::hal_audio>
        $<$<BOOL:${MW_HAL_CAMERA}>:  mw::hal_camera>
        $<$<BOOL:${MW_HAL_GPIO}>:    mw::hal_gpio>
        $<$<BOOL:${MW_HAL_SENSORS}>: mw::hal_sensors>
)

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
set(_hal_install_targets mw_hal mw_hal_iface)
foreach(_layer audio camera gpio sensors)
    if(TARGET mw_hal_${_layer})
        list(APPEND _hal_install_targets mw_hal_${_layer})
    endif()
endforeach()

install(
    TARGETS ${_hal_install_targets}
    EXPORT  linux_middleware_targets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}" COMPONENT development
    FILE_SET HEADERS
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/mw/hal"
    COMPONENT   development
)
