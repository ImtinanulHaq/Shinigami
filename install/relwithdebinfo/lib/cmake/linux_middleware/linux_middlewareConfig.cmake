# =============================================================================
# linux_middlewareConfig.cmake.in
# CMake package config — used by find_package(linux_middleware).
#
# Downstream usage:
#   find_package(linux_middleware 2.0 REQUIRED COMPONENTS core hal proxy)
#   target_link_libraries(myapp PRIVATE mw::proxy mw::hal)
# =============================================================================


####### Expanded from @PACKAGE_INIT@ by configure_package_config_file() #######
####### Any changes to this file will be overwritten by the next CMake run ####
####### The input file was linux_middlewareConfig.cmake.in                            ########

get_filename_component(PACKAGE_PREFIX_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../" ABSOLUTE)

macro(set_and_check _var _file)
  set(${_var} "${_file}")
  if(NOT EXISTS "${_file}")
    message(FATAL_ERROR "File or directory ${_file} referenced by variable ${_var} does not exist !")
  endif()
endmacro()

macro(check_required_components _NAME)
  foreach(comp ${${_NAME}_FIND_COMPONENTS})
    if(NOT ${_NAME}_${comp}_FOUND)
      if(${_NAME}_FIND_REQUIRED_${comp})
        set(${_NAME}_FOUND FALSE)
      endif()
    endif()
  endforeach()
endmacro()

####################################################################################

# ---------------------------------------------------------------------------
# Path variables (relocated by CMake's @PACKAGE_* mechanism)
# ---------------------------------------------------------------------------
set_and_check(linux_middleware_INCLUDE_DIR "${PACKAGE_PREFIX_DIR}/include")
set_and_check(linux_middleware_LIB_DIR     "${PACKAGE_PREFIX_DIR}/lib")

# ---------------------------------------------------------------------------
# Required external dependencies — must find them before importing our targets
# ---------------------------------------------------------------------------
include(CMakeFindDependencyMacro)

find_dependency(Threads REQUIRED)
find_dependency(OpenSSL 3.0 REQUIRED COMPONENTS Crypto)
# liburing via pkg-config (mirrors root CMakeLists.txt approach)
find_package(PkgConfig REQUIRED)
pkg_check_modules(PC_LIBURING REQUIRED liburing)
if(NOT TARGET LibUring::LibUring)
    add_library(LibUring::LibUring INTERFACE IMPORTED)
    set_target_properties(LibUring::LibUring PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${PC_LIBURING_INCLUDE_DIRS}"
        INTERFACE_LINK_LIBRARIES      "${PC_LIBURING_LINK_LIBRARIES}"
    )
endif()
find_dependency(ALSA REQUIRED)
find_dependency(LibSeccomp 2.5 REQUIRED)
find_dependency(LibCap 2.0 REQUIRED)

# ---------------------------------------------------------------------------
# Import the exported targets
# ---------------------------------------------------------------------------
include("${CMAKE_CURRENT_LIST_DIR}/linux_middlewareTargets.cmake")

# ---------------------------------------------------------------------------
# Component checking
# ---------------------------------------------------------------------------
set(_mw_components core hal security sm proxy monitoring)

foreach(_comp ${linux_middleware_FIND_COMPONENTS})
    if(NOT _comp IN_LIST _mw_components)
        set(linux_middleware_NOT_FOUND_MESSAGE
            "linux_middleware: unknown component '${_comp}'. "
            "Valid components: ${_mw_components}")
        set(linux_middleware_FOUND FALSE)
        return()
    endif()

    # Map component name to the ALIAS target
    if(NOT TARGET mw::${_comp})
        set(linux_middleware_${_comp}_FOUND FALSE)
        if(linux_middleware_FIND_REQUIRED_${_comp})
            set(linux_middleware_NOT_FOUND_MESSAGE
                "linux_middleware: required component '${_comp}' not found "
                "(target mw::${_comp} not in this installation).")
            set(linux_middleware_FOUND FALSE)
            return()
        endif()
    else()
        set(linux_middleware_${_comp}_FOUND TRUE)
    endif()
endforeach()

check_required_components(linux_middleware)
