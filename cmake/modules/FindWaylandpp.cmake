# FindWaylandpp
# -----------
# Finds the waylandpp library
#
# This will will define the following variables::
#
# WAYLANDPP_FOUND        - the system has Wayland
# WAYLANDPP_INCLUDE_DIRS - the Wayland include directory
# WAYLANDPP_LIBRARIES    - the Wayland libraries
# WAYLANDPP_DEFINITIONS  - the Wayland definitions


if(PKG_CONFIG_FOUND)
  pkg_check_modules (PC_WAYLANDPP waylandpp QUIET)
endif()

find_path(WAYLANDPP_INCLUDE_DIR NAMES wayland-client.hpp
                                PATHS ${PC_WAYLANDPP_INCLUDE_DIRS})

find_library(WAYLANDPP_CLIENT_LIBRARY NAMES wayland-client++
                                      PATHS ${PC_WAYLANDPP_LIBRARIES} ${PC_WAYLANDPP_LIBRARY_DIRS})

find_library(WAYLANDPP_CURSOR_LIBRARY NAMES wayland-cursor++
                                      PATHS ${PC_WAYLANDPP_LIBRARIES} ${PC_WAYLANDPP_LIBRARY_DIRS})

find_library(WAYLANDPP_EGL_LIBRARY NAMES wayland-egl++
                                   PATHS ${PC_WAYLANDPP_LIBRARIES} ${PC_WAYLANDPP_LIBRARY_DIRS})

include (FindPackageHandleStandardArgs)
find_package_handle_standard_args (Waylandpp
  REQUIRED_VARS
  WAYLANDPP_INCLUDE_DIR
  WAYLANDPP_CLIENT_LIBRARY
  WAYLANDPP_CURSOR_LIBRARY
  WAYLANDPP_EGL_LIBRARY)

if (WAYLANDPP_FOUND)
  set(WAYLANDPP_LIBRARIES ${WAYLANDPP_CLIENT_LIBRARY} ${WAYLANDPP_CURSOR_LIBRARY} ${WAYLANDPP_EGL_LIBRARY})
  set(WAYLANDPP_INCLUDE_DIRS ${PC_WAYLANDPP_INCLUDE_DIRS})
  set(WAYLANDPP_DEFINITIONS -DHAVE_WAYLAND=1)
endif()

mark_as_advanced (WAYLANDPP_CLIENT_LIBRARY WAYLANDPP_CURSOR_LIBRARY WAYLANDPP_EGL_LIBRARY WAYLANDPP_INCLUDE_DIR)
