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

#include "WinSystemWaylandGLContext.h"

#include "Connection.h"
#include "utils/log.h"

using namespace KODI::WINDOWING::WAYLAND;


bool CWinSystemWaylandGLContext::InitWindowSystem()
{
  if (!CWinSystemWayland::InitWindowSystem())
  {
    return false;
  }

  return m_glContext.CreateDisplay(m_connection->GetDisplay(),
                                   EGL_OPENGL_BIT,
                                   EGL_OPENGL_API);
}

bool CWinSystemWaylandGLContext::CreateNewWindow(const std::string& name,
                                                 bool fullScreen,
                                                 RESOLUTION_INFO& res)
{

  if (!CWinSystemWayland::CreateNewWindow(name, fullScreen, res))
  {
    return false;
  }

  if (!m_glContext.CreateSurface(m_surface, res.iWidth, res.iHeight))
  {
    return false;
  }

  return SetFullScreen(fullScreen, res, false);
}

bool CWinSystemWaylandGLContext::DestroyWindow()
{
  m_glContext.DestroySurface();

  return CWinSystemWayland::DestroyWindow();
}

bool CWinSystemWaylandGLContext::DestroyWindowSystem()
{
  m_glContext.Destroy();

  return CWinSystemWayland::DestroyWindowSystem();
}

bool CWinSystemWaylandGLContext::SetFullScreen(bool fullScreen, RESOLUTION_INFO& res, bool blankOtherDisplays)
{
  // Resize the native window so Wayland has a chance of knowing what
  // size we want
  m_glContext.Resize(res.iWidth, res.iHeight);
  
  if (!CWinSystemWayland::SetFullScreen(fullScreen, res, blankOtherDisplays))
  {
    return false;
  }
  
  if (!CRenderSystemGL::ResetRenderSystem(res.iWidth, res.iHeight, fullScreen, res.fRefreshRate))
  {
    return false;
  }
  
  return true;
}

void CWinSystemWaylandGLContext::HandleSurfaceConfigure(std::int32_t width, std::int32_t height)
{
  // Wayland will tell us here the size of the surface that was actually created,
  // which might be different from what we expected e.g. when fullscreening
  // on an output we chose - the compositor might have decided to use a different
  // output for example
  // It is very important that the EGL native module and the rendering system use the
  // Wayland-announced size for rendering or corrupted graphics output will result.
  int currWidth, currHeight;
  m_glContext.GetAttachedSize(currWidth, currHeight);
  bool changed = (width != currWidth || height != currHeight);
  
  if (changed)
  {
    m_glContext.Resize(width, height);
  }
  CWinSystemWayland::HandleSurfaceConfigure(width, height);
  if (changed)
  {
    CRenderSystemGL::ResetRenderSystem(width, height, m_bFullScreen, m_fRefreshRate);
  }
}

void CWinSystemWaylandGLContext::SetVSyncImpl(bool enable)
{
  m_glContext.SetVSync(enable);
}

void CWinSystemWaylandGLContext::PresentRenderImpl(bool rendered)
{
  if (rendered)
  {
    m_glContext.SwapBuffers();
  }
}

EGLDisplay CWinSystemWaylandGLContext::GetEGLDisplay() const
{
  return m_glContext.m_eglDisplay;
}

EGLSurface CWinSystemWaylandGLContext::GetEGLSurface() const
{
  return m_glContext.m_eglSurface;
}

EGLContext CWinSystemWaylandGLContext::GetEGLContext() const
{
  return m_glContext.m_eglContext;
}

EGLConfig CWinSystemWaylandGLContext::GetEGLConfig() const
{
  return m_glContext.m_eglConfig;
}