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

#include "Connection.h"
#include "ShellSurface.h"

#include <wayland-extra-protocols.hpp>

namespace KODI
{
namespace WINDOWING
{
namespace WAYLAND
{

/**
 * Abstraction for shell surfaces to support multiple protocols
 * such as wl_shell (for compatibility) and xdg_shell (for features)
 */
class CShellSurfaceXdgShellUnstableV6 : public IShellSurface
{
public:
  /**
   * Construct wl_shell_surface for given surface
   * 
   * \param display the wl_display global (for initial roundtrip)
   * \param shell wl_shell global
   * \param surface surface to make shell surface for
   * \param title title of the surfae
   * \param class_ class of the surface, which should match the name of the
   *               .desktop file of the application
   */
  CShellSurfaceXdgShellUnstableV6(wayland::display_t& display, wayland::zxdg_shell_v6_t const& shell, wayland::surface_t const& surface, std::string title, std::string class_);
  virtual ~CShellSurfaceXdgShellUnstableV6();

  static CShellSurfaceXdgShellUnstableV6* TryCreate(CConnection& connection, wayland::surface_t const& surface, std::string title, std::string class_);
  
  void Initialize() override;
  
  void SetFullScreen(wayland::output_t const& output, float refreshRate) override;
  void SetWindowed() override;
  void SetMaximized() override;
  void UnsetMaximized() override;
  void SetMinimized() override;
  void SetWindowGeometry(CRectInt geometry) override;
  void AckConfigure(std::uint32_t serial) override;

  void StartMove(const wayland::seat_t& seat, std::uint32_t serial) override;
  void StartResize(const wayland::seat_t& seat, std::uint32_t serial, wayland::shell_surface_resize edge) override;
  void ShowShellContextMenu(const wayland::seat_t& seat, std::uint32_t serial, CPointInt position) override;

private:
  wayland::display_t& m_display;
  wayland::zxdg_shell_v6_t m_shell;
  wayland::surface_t m_surface;
  wayland::zxdg_surface_v6_t m_xdgSurface;
  wayland::zxdg_toplevel_v6_t m_xdgToplevel;

  CSizeInt m_configuredSize;
  StateBitset m_configuredState;
};

}
}
}