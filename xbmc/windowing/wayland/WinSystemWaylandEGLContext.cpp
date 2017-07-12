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

#include "WinSystemWaylandEGLContext.h"

#include "Connection.h"
#include "cores/VideoPlayer/DVDCodecs/DVDFactoryCodec.h"
#include "cores/VideoPlayer/VideoRenderers/RenderFactory.h"
#include "utils/log.h"
#include "guilib/GraphicContext.h"

using namespace KODI::WINDOWING::WAYLAND;

bool CWinSystemWaylandEGLContext::InitWindowSystemEGL(EGLint renderableType, EGLint apiType)
{
  VIDEOPLAYER::CRendererFactory::ClearRenderer();
  CDVDFactoryCodec::ClearHWAccels();

  if (!CWinSystemWayland::InitWindowSystem())
  {
    return false;
  }

  if (!m_eglContext.CreateDisplay(m_connection->GetDisplay(), renderableType, apiType))
  {
    return false;
  }

  return true;
}

bool CWinSystemWaylandEGLContext::CreateNewWindow(const std::string& name,
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
  if (!m_eglContext.CreateSurface(m_surface, m_nWidth, m_nHeight))
  {
    return false;
  }

  return true;
}

bool CWinSystemWaylandEGLContext::DestroyWindow()
{
  m_eglContext.DestroySurface();

  return CWinSystemWayland::DestroyWindow();
}

bool CWinSystemWaylandEGLContext::DestroyWindowSystem()
{
  m_eglContext.Destroy();

  return CWinSystemWayland::DestroyWindowSystem();
}

bool CWinSystemWaylandEGLContext::SetFullScreen(bool fullScreen, RESOLUTION_INFO& res, bool blankOtherDisplays)
{
  // FIXME See CWinSystemWayland::SetFullScreen()
  CSingleLock lock(g_graphicsContext);

  if (!CWinSystemWayland::SetFullScreen(fullScreen, res, blankOtherDisplays))
  {
    return false;
  }

  // Look only at m_nWidth and m_nHeight which represent the actual wl_surface
  // size instead of res.iWidth and res.iHeight, which are only a "wish"

  int currWidth, currHeight;
  m_eglContext.GetAttachedSize(currWidth, currHeight);

  // Change EGL surface size if necessary
  if (currWidth != m_nWidth || currHeight != m_nHeight)
  {
    CLog::LogF(LOGDEBUG, "Updating egl_window size to %dx%d", m_nWidth, m_nHeight);
    m_eglContext.Resize(m_nWidth, m_nHeight);
  }

  return true;
}

EGLDisplay CWinSystemWaylandEGLContext::GetEGLDisplay() const
{
  return m_eglContext.m_eglDisplay;
}