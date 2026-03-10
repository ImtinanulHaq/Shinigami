# =============================================================================
# dev/services/CMakeLists.txt
# Hardware service daemons top-level dispatcher.
#
# Build order enforced by CMake dependency graph:
#   mw_svc_common → all four service daemons
#
# Each daemon links: mw::svc_common + its HAL layer + mw::sm (client API)
#                    + mw::security + mw::core
# =============================================================================

if(NOT MW_BUILD_SERVICES)
    message(STATUS "[MW] Services: skipped (MW_BUILD_SERVICES=OFF)")
    return()
endif()

# Common service infrastructure first
add_subdirectory(common)

# Individual service daemons — only built if the corresponding HAL is enabled
if(MW_HAL_AUDIO)
    add_subdirectory(audio_service)
endif()

if(MW_HAL_CAMERA)
    add_subdirectory(camera_service)
endif()

if(MW_HAL_GPIO)
    add_subdirectory(gpio_service)
endif()

if(MW_HAL_SENSORS)
    add_subdirectory(sensor_service)
endif()
