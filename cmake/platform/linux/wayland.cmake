set(PLATFORM_REQUIRED_DEPS OpenGl EGL Waylandpp LibDRM Xkbcommon)
set(PLATFORM_OPTIONAL_DEPS VAAPI)
set(PLATFORM_GLOBAL_TARGET_DEPS generate-wayland-extra-protocols)
set(WAYLAND_EXTRA_PROTOCOL_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}")
# for wayland-extra-protocols.hpp
include_directories("${WAYLAND_EXTRA_PROTOCOL_GENERATED_DIR}")
