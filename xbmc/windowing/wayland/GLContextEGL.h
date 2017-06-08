/*
 *      Copyright (C) 2017 Team XBMC
 *      http://xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */
#pragma once

#include <set>
#include <string>

#include <wayland-client.hpp>
#include <wayland-egl.hpp>
#include <EGL/egl.h>
#include <EGL/eglext.h>

namespace KODI
{
namespace WINDOWING
{
namespace WAYLAND
{

/**
 * Class that handles EGL setup/teardown with Wayland native surfaces
 */
class CGLContextEGL
{
public:
  CGLContextEGL();
  virtual ~CGLContextEGL();

  bool CreateDisplay(wayland::display_t& display,
                     EGLint renderable_type,
                     EGLint rendering_api);

  bool CreateSurface(wayland::surface_t& surface, int width, int height);
  void GetAttachedSize(int& width, int& height);
  void Resize(int width, int height);
  void DestroySurface();
  void Destroy();
  void SetVSync(bool enable);
  void SwapBuffers();

  wayland::egl_window_t m_nativeWindow;
  EGLDisplay m_eglDisplay = EGL_NO_DISPLAY;
  EGLSurface m_eglSurface = EGL_NO_SURFACE;
  EGLContext m_eglContext = EGL_NO_CONTEXT;
  EGLConfig m_eglConfig = nullptr;
  
private:
  CGLContextEGL(CGLContextEGL const& other) = delete;
  CGLContextEGL& operator=(CGLContextEGL const& other) = delete;
  
  std::set<std::string> m_clientExtensions;
  
  PFNEGLGETPLATFORMDISPLAYEXTPROC m_eglGetPlatformDisplayEXT = nullptr;
  PFNEGLCREATEPLATFORMWINDOWSURFACEEXTPROC m_eglCreatePlatformWindowSurfaceEXT = nullptr;
};

}
}
}
