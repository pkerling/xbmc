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
#include <list>
#include <map>
#include <memory>

#include <wayland-client.hpp>

namespace KODI
{
namespace WINDOWING
{
namespace WAYLAND
{

/**
 * Handler interface for \ref CConnection
 */
class IConnectionHandler
{
public:
  virtual ~IConnectionHandler() {}
  virtual void OnSeatAdded(std::uint32_t name, wayland::seat_t& seat) {}
  virtual void OnOutputAdded(std::uint32_t name, wayland::output_t& output) {}
  virtual void OnGlobalRemoved(std::uint32_t name) {}
};

/**
 * Wayland connection state manager
 * 
 * Listens for global interface add/remove add passes corresponding information
 * to handler
 */
class CConnection
{
public:
  explicit CConnection(IConnectionHandler* handler);
  
  wayland::display_t& GetDisplay();
  wayland::compositor_t& GetCompositor();
  wayland::shell_t& GetShell();
  wayland::shm_t& GetShm();  
  
private:
  void CheckRequiredGlobals();
  void HandleRegistry();
  
  IConnectionHandler* m_handler;
  
  std::unique_ptr<wayland::display_t> m_display;
  
  struct InterfaceBindInfo
  {
    wayland::proxy_t& target;
    std::uint32_t bindVersion;
    bool required = true;
    InterfaceBindInfo(wayland::proxy_t& target, std::uint32_t bindVersion)
      : target(target), bindVersion(bindVersion) {}
  };
  std::map<std::string, InterfaceBindInfo> m_binds;
  
  wayland::registry_t m_registry;
  wayland::compositor_t m_compositor;
  wayland::shell_t m_shell;
  wayland::shm_t m_shm;
};

}
}
}