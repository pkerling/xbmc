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

  // CWinSystemWayland::CreateNewWindow sets internal m_nWidth and m_nHeight
  // to the resolution that should be used for the initial surface size
  // - the compositor might want something other than the resolution given
  if (!m_glContext.CreateSurface(m_surface, m_nWidth, m_nHeight))
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
  auto width = res.iWidth;
  auto height = res.iHeight;
  
  int currWidth, currHeight;
  m_glContext.GetAttachedSize(currWidth, currHeight);

  if (width != currWidth || height != currHeight)
  {
    m_glContext.Resize(width, height);
  }
  
  if (!CWinSystemWayland::SetFullScreen(fullScreen, res, blankOtherDisplays))
  {
    return false;
  }
  
  if (!CRenderSystemGL::ResetRenderSystem(width, height, fullScreen, res.fRefreshRate))
  {
    return false;
  }
  
  return true;
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