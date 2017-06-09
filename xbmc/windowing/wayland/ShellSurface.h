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

#include <cstdint>
#include <functional>

#include <wayland-client.hpp>

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
class IShellSurface
{
protected:
  void InvokeOnConfigure(std::int32_t width, std::int32_t height);
  
public:
  /**
   * Construct shell surface over normal surface
   * 
   * The complete argument list depends on the shell implementation, but it
   * will generally include a \ref wayland::surface_t that is used as
   * base surface which is decorated with the corresponding shell surface role.
   * 
   * The event loop thread MUST NOT be running when constructors of implementing
   * classes are called.
   */
  IShellSurface() {}
  virtual ~IShellSurface() {}
  
  /**
   * Initialize shell surface
   * 
   * The event loop thread MUST NOT be running when this function is called.
   * The difference to the constructor is that in this function callbacks may
   * already be called.
   */
  virtual void Initialize() = 0;
  
  using ConfigureHandler = std::function<void(std::int32_t, std::int32_t)>;
  
  virtual void SetFullScreen(wayland::output_t const& output, float refreshRate) = 0;
  virtual void SetWindowed() = 0;
  
  ConfigureHandler& OnConfigure();
  
private:
  ConfigureHandler m_onConfigure;
  
  IShellSurface(IShellSurface const& other) = delete;
  IShellSurface& operator=(IShellSurface const& other) = delete;
};

}
}
}