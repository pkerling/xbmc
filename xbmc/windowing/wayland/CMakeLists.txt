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
            ${WAYLAND_EXTRA_PROTOCOL_GENERATED_DIR}/wayland-extra-protocols.cpp
            WinEventsWayland.cpp
            WinSystemWayland.cpp
            WinSystemWaylandGLContext.cpp)

set(HEADERS Connection.h
            Output.h
            GLContextEGL.h
            SeatInputProcessor.h
            ShellSurface.h
            ShellSurfaceWlShell.h
            ${WAYLAND_EXTRA_PROTOCOL_GENERATED_DIR}/wayland-extra-protocols.hpp
            WinEventsWayland.h
            WinSystemWayland.h
            WinSystemWaylandGLContext.h)

core_add_library(windowing_WAYLAND)