set(SOURCES Connection.cpp
            Output.cpp
            GLContextEGL.cpp
            SeatInputProcessor.cpp
            WinEventsWayland.cpp
            WinSystemWayland.cpp
            WinSystemWaylandGLContext.cpp)

set(HEADERS Connection.h
            Output.h
            GLContextEGL.h
            SeatInputProcessor.h
            WinEventsWayland.h
            WinSystemWayland.h
            WinSystemWaylandGLContext.h)

core_add_library(windowing_WAYLAND)
