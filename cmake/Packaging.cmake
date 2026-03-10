# =============================================================================
# cmake/Packaging.cmake
# CPack configuration for .deb, .rpm, and .tar.gz packages.
# Activated via cmake --build build --target package
# or via package presets in CMakePresets.json.
# =============================================================================

include_guard(GLOBAL)

set(CPACK_PACKAGE_NAME              "linux-middleware")
set(CPACK_PACKAGE_VENDOR            "linux-middleware project")
set(CPACK_PACKAGE_DESCRIPTION_SHORT "Embedded Linux hardware middleware platform")
set(CPACK_PACKAGE_DESCRIPTION       "Multi-process middleware managing audio, camera, GPIO, and sensor hardware over a hardened IPC layer with io_uring I/O, HMAC-authenticated service manager, and seccomp sandboxing.")
set(CPACK_PACKAGE_CONTACT           "maintainers@linux-middleware.io")
set(CPACK_PACKAGE_HOMEPAGE_URL      "https://github.com/org/linux-middleware")

# Version comes from project() — already set
set(CPACK_PACKAGE_VERSION_MAJOR     "${PROJECT_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR     "${PROJECT_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH     "${PROJECT_VERSION_PATCH}")

set(CPACK_RESOURCE_FILE_LICENSE     "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README      "${CMAKE_SOURCE_DIR}/README.md")

# Output goes to build/packages
set(CPACK_OUTPUT_FILE_PREFIX        "${CMAKE_BINARY_DIR}/packages")

# Only package the install set, not the source tree
set(CPACK_PACKAGE_DIRECTORY         "${CMAKE_BINARY_DIR}/packages")
set(CPACK_STRIP_FILES               TRUE)

# Source package exclusions (for source tarballs)
set(CPACK_SOURCE_IGNORE_FILES
    "/.git/"
    "/build/"
    "/install/"
    "/packages/"
    "\\.swp$"
    "\\.orig$"
)

# ---------------------------------------------------------------------------
# DEB package
# ---------------------------------------------------------------------------
set(CPACK_DEBIAN_PACKAGE_MAINTAINER   "linux-middleware maintainers <maintainers@linux-middleware.io>")
set(CPACK_DEBIAN_PACKAGE_SECTION      "libs")
set(CPACK_DEBIAN_PACKAGE_PRIORITY     "optional")
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "${CMAKE_SYSTEM_PROCESSOR}")

# Runtime dependencies — these are the library package names on Debian/Ubuntu
set(CPACK_DEBIAN_PACKAGE_DEPENDS
    "liburing2 (>= 2.3), libssl3 (>= 3.0), libseccomp2 (>= 2.5), libcap2 (>= 1:2.44), libasound2 (>= 1.2), libncurses6"
)

# Conffiles — config files that should not be overwritten on upgrade
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
    "${CMAKE_SOURCE_DIR}/packaging/deb/postinst"
    "${CMAKE_SOURCE_DIR}/packaging/deb/prerm"
)

set(CPACK_DEB_COMPONENT_INSTALL ON)

# Separate runtime / development / monitoring packages
set(CPACK_DEBIAN_RUNTIME_PACKAGE_NAME     "linux-middleware")
set(CPACK_DEBIAN_DEVELOPMENT_PACKAGE_NAME "linux-middleware-dev")
set(CPACK_DEBIAN_MONITORING_PACKAGE_NAME  "linux-middleware-monitor")

# ---------------------------------------------------------------------------
# RPM package
# ---------------------------------------------------------------------------
set(CPACK_RPM_PACKAGE_RELEASE      "1")
set(CPACK_RPM_PACKAGE_LICENSE      "MIT")
set(CPACK_RPM_PACKAGE_GROUP        "System/Libraries")
set(CPACK_RPM_PACKAGE_ARCHITECTURE "${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_RPM_PACKAGE_SUMMARY      "${CPACK_PACKAGE_DESCRIPTION_SHORT}")
set(CPACK_RPM_PACKAGE_REQUIRES
    "liburing >= 2.3, openssl-libs >= 3.0, libseccomp >= 2.5, libcap >= 2.44, alsa-lib >= 1.2, ncurses-libs"
)
set(CPACK_RPM_COMPONENT_INSTALL ON)

# ---------------------------------------------------------------------------
# TGZ (generic tarball — works on any distro)
# ---------------------------------------------------------------------------
set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)

# ---------------------------------------------------------------------------
# Component definitions
# Runtime: daemons, shared libs
# Development: headers, static libs, CMake exports
# Monitoring: monitord + TUI
# ---------------------------------------------------------------------------
include(CPackComponent)

cpack_add_component(runtime
    DISPLAY_NAME "Runtime"
    DESCRIPTION  "Service manager daemon, hardware service daemons, and runtime libraries"
    REQUIRED
)

cpack_add_component(development
    DISPLAY_NAME "Development"
    DESCRIPTION  "Headers, static libraries, and CMake package config for building against linux-middleware"
    DEPENDS runtime
)

cpack_add_component(monitoring
    DISPLAY_NAME "Monitoring"
    DESCRIPTION  "monitord daemon, TUI, and Prometheus metrics exporter"
    DEPENDS runtime
)

include(CPack)
