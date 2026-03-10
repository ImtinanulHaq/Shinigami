##############################################################################
# services/CMakeLists.txt  — BUILD WRAPPER ONLY
#
# All source code lives in:  dev/services/
# This file is the build entry-point kept here for convenience.
#
# Usage:
#   cmake -B services/build -S services
#   cmake --build services/build
#   ctest --test-dir services/build
#
# To build directly from the source tree instead:
#   cmake -B dev/services/build -S dev/services
##############################################################################

cmake_minimum_required(VERSION 3.16)
project(platform_services_build C)

# Delegate everything to the real source in dev/services/
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../dev/services
                 ${CMAKE_CURRENT_BINARY_DIR}/platform_services)
