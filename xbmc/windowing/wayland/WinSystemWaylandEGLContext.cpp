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

void CWinSystemWaylandEGLContext::PresentFrame(bool rendered)
{
  // We let the egl driver handle the specifics of frame synchronization/throttling.
  // Currently, in eglSwapBuffers() mesa
  // 1. waits until a frame() drawing hint from the compositor arrives,
  // 2. then commits the backbuffer to the surface, and
  // 3. finally requests a new frame() hint for the next presentation and immediately
  //    returns, i.e. drawing can start again
  // This means that rendering is optimized for maximum time available for
  // our repaint and reliable timing rather than latency. With weston, latency
  // will usually be on the order of two frames plus a few milliseconds.
  //
  // If it turns out that other egl drivers do things differently and we don't like
  // it, it's always a possibility to set eglSwapInterval(0) and handle frame
  // callbacks here ourselves.
  //
  // The frame timings become irregular though when nothing is rendered because
  // kodi then sleeps for a fixed time without swapping buffers. This makes mesa
  // immediately attach the next buffer because the frame callback has already arrived when
  // eglSwapBuffers() is called and step 1. above is skipped. As we render with full
  // FPS during video playback anyway and the timing is otherwise not relevant,
  // this should not be a problem.

  PrepareFramePresentation();

  if (rendered)
  {
    m_eglContext.SwapBuffers();
    // eglSwapBuffers() (hopefully) calls commit on the surface and flushes
    // ... well mesa does anyway
  }
  else
  {
    // For presentation feedback: Get notification of the next vblank even
    // when contents did not change
    m_surface.commit();
    // Make sure it reaches the compositor
    m_connection->GetDisplay().flush();
  }
}

EGLDisplay CWinSystemWaylandEGLContext::GetEGLDisplay() const
{
  return m_eglContext.m_eglDisplay;
}