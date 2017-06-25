# from ArchSetup.cmake
set_source_files_properties(${WAYLAND_EXTRA_PROTOCOL_GENERATED_DIR}/wayland-extra-protocols.cpp
                            ${WAYLAND_EXTRA_PROTOCOL_GENERATED_DIR}/wayland-extra-protocols.hpp
                            PROPERTIES GENERATED TRUE)

set(SOURCES Connection.cpp
            Output.cpp
            GLContextEGL.cpp
            SeatInputProcessor.cpp
            ShellSurface.cpp
            ShellSurfaceWlShell.cpp
            ShellSurfaceXdgShellUnstableV6.cpp
            ${WAYLAND_EXTRA_PROTOCOL_GENERATED_DIR}/wayland-extra-protocols.cpp
            WinEventsWayland.cpp
            WinSystemWayland.cpp
            WinSystemWaylandEGLContext.cpp)

set(HEADERS Connection.h
            Output.h
            GLContextEGL.h
            SeatInputProcessor.h
            ShellSurface.h
            ShellSurfaceWlShell.h
            ShellSurfaceXdgShellUnstableV6.h
            ${WAYLAND_EXTRA_PROTOCOL_GENERATED_DIR}/wayland-extra-protocols.hpp
            WinEventsWayland.h
            WinSystemWayland.h
            WinSystemWaylandEGLContext.h)

if(OPENGL_FOUND)
  list(APPEND SOURCES WinSystemWaylandEGLContextGL.cpp)
  list(APPEND HEADERS WinSystemWaylandEGLContextGL.h)
endif()
if(OPENGLES_FOUND)
  list(APPEND SOURCES WinSystemWaylandEGLContextGLES.cpp)
  list(APPEND HEADERS WinSystemWaylandEGLContextGLES.h)
endif()


core_add_library(windowing_WAYLAND)