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

#include "messaging/ApplicationMessenger.h"
#include "ShellSurfaceXdgShellUnstableV6.h"

using namespace KODI::WINDOWING::WAYLAND;

CShellSurfaceXdgShellUnstableV6::CShellSurfaceXdgShellUnstableV6(wayland::display_t& display, const wayland::zxdg_shell_v6_t& shell, const wayland::surface_t& surface, std::string title, std::string app_id)
: m_display(&display), m_shell(shell), m_surface(surface), m_xdgSurface(m_shell.get_xdg_surface(m_surface)), m_xdgToplevel(m_xdgSurface.get_toplevel())
{
  m_shell.on_ping() = [this](std::uint32_t serial)
  {
    m_shell.pong(serial);
  };
  m_xdgSurface.on_configure() = [this](std::uint32_t serial)
  {
    // xdg_toplevel may send width/height 0 if it has no idea how big it wants the
    // surface to be
    if (m_configuredWidth != 0 && m_configuredHeight != 0)
    {
      InvokeOnConfigure(m_configuredWidth, m_configuredHeight);
    }
    m_xdgSurface.ack_configure(serial);
  };
  m_xdgToplevel.on_close() = [this]()
  {
    MESSAGING::CApplicationMessenger::GetInstance().PostMsg(TMSG_QUIT);
  };
  m_xdgToplevel.on_configure() = [this](std::int32_t width, std::int32_t height, wayland::array_t)
  {
    m_configuredWidth = width;
    m_configuredHeight = height;
  };
  m_xdgToplevel.set_app_id(app_id);
  m_xdgToplevel.set_title(title);
}

void CShellSurfaceXdgShellUnstableV6::Initialize()
{
  // Commit surface to confirm role
  // Don't do it in constructor since SetFullScreen might be called before
  m_surface.commit();
  // Make sure we get the initial configure before continuing
  m_display->roundtrip();
}

CShellSurfaceXdgShellUnstableV6::~CShellSurfaceXdgShellUnstableV6()
{
  // xdg_shell is picky: must destroy toplevel role before surface
  m_xdgToplevel.proxy_release();
  m_xdgSurface.proxy_release();
}

void CShellSurfaceXdgShellUnstableV6::SetFullScreen(const wayland::output_t& output, float)
{
  // xdg_shell does not support refresh rate setting at the moment
  
  // mutter has some problems with setting the same output again, so only
  // call set_fullscreen() if the output changes
  // https://bugzilla.gnome.org/show_bug.cgi?id=783709
  if (output != m_currentOutput)
  {
    m_xdgToplevel.set_fullscreen(output);
    m_currentOutput = output;
  }
}

void CShellSurfaceXdgShellUnstableV6::SetWindowed()
{
  m_currentOutput = wayland::output_t();
  m_xdgToplevel.unset_fullscreen();
}
