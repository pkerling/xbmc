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

#include "Connection.h"

#include <cassert>
#include <map>

#include "utils/log.h"

using namespace KODI::WINDOWING::WAYLAND;

namespace
{

void Bind(wayland::registry_t& registry, wayland::proxy_t& target, std::uint32_t name, std::string const& interface, std::uint32_t minVersion, std::uint32_t maxVersion, std::uint32_t offeredVersion)
{
  if (offeredVersion < minVersion)
  {
    throw std::runtime_error(std::string("Wayland server has version ") + std::to_string(offeredVersion) + " of protocol " + interface + ", but we need at least version " + std::to_string(minVersion));
  }
  // Binding below the offered version is OK
  auto bindVersion = std::min(maxVersion, offeredVersion);
  CLog::Log(LOGDEBUG, "Binding Wayland protocol %s version %u (server has version %u)", interface.c_str(), bindVersion, offeredVersion);
  registry.bind(name, target, bindVersion);
}

}

CConnection::CConnection(IConnectionHandler* handler)
: m_handler(handler)
{
  m_display.reset(new wayland::display_t); 
}

void CConnection::BindSingletons()
{
  // Do two rounds: First get global singleton interfaces, then seats and outputs
  // Processing for seats depends on the data device manager being available,
  // so making sure it is there first simplifies processing

  m_binds = {
    // version 3 adds wl_surface::set_buffer_scale
    // version 4 adds wl_surface::damage_buffer
    { wayland::compositor_t::interface_name, { m_compositor, 1, 4 } },
    { wayland::shell_t::interface_name, { m_shell, 1, 1 } },
    { wayland::shm_t::interface_name, { m_shm, 1, 1 } },
    { wayland::data_device_manager_t::interface_name, { m_dataDeviceManager, 1, 3, false } },
    { wayland::subcompositor_t::interface_name, { m_subcompositor, 1, 1, false } },
    { wayland::zxdg_shell_v6_t::interface_name, { m_xdgShellUnstableV6, 1, 1, false } },
    { wayland::zwp_idle_inhibit_manager_v1_t::interface_name, { m_idleInhibitManagerUnstableV1, 1, 1, false } },
    { wayland::presentation_t::interface_name, { m_presentation, 1, 1, false } }
  };

  m_registry = m_display->get_registry();
  HandleRegistry(true, false);
  
  CLog::Log(LOGDEBUG, "Wayland connection: Waiting for global singleton interfaces");
  m_display->roundtrip();
  CheckRequiredGlobals();
  CLog::Log(LOGDEBUG, "Wayland connection: Initial roundtrip complete");
}

void CConnection::BindOther()
{
  m_registry = m_display->get_registry();
  HandleRegistry(false, true);
  CLog::Log(LOGDEBUG, "Wayland connection: Waiting for global seats and outputs");
  m_display->roundtrip();
  CLog::Log(LOGDEBUG, "Wayland connection: Initial roundtrip complete");
}

void CConnection::HandleRegistry(bool bindSingletons, bool bindNonSingletons)
{
  m_registry.on_global() = [=] (std::uint32_t name, std::string interface, std::uint32_t version)
  { 
    auto it = m_binds.find(interface);
    if (bindSingletons && it != m_binds.end())
    {
      Bind(m_registry, it->second.target, name, interface, it->second.minVersion, it->second.maxVersion, version);
    }
    else if (bindNonSingletons && interface == wayland::seat_t::interface_name)
    {
      wayland::seat_t seat;
      // version 2 adds name event, optional
      // version 4 adds wl_keyboard repeat_info, optional
      // version 5 adds discrete axis events in wl_pointer
      Bind(m_registry, seat, name, interface, 1, 5, version);
      m_handler->OnSeatAdded(name, seat);
    }
    else if (bindNonSingletons && interface == wayland::output_t::interface_name)
    {
      wayland::output_t output;
      // version 2 adds done(), required
      // version 3 only adds destructor
      Bind(m_registry, output, name, interface, 2, 3, version);
      m_handler->OnOutputAdded(name, output);
    }
  };
  m_registry.on_global_remove() = [this] (std::uint32_t name)
  {
    m_handler->OnGlobalRemoved(name);
  };
}

void CConnection::CheckRequiredGlobals()
{
  for (auto const& bind : m_binds)
  {
    if (bind.second.required && !bind.second.target)
    {
      throw std::runtime_error(std::string("Missing required ") + bind.first + " protocol");
    }
  }
}

wayland::display_t& CConnection::GetDisplay()
{
  assert(m_display);
  return *m_display;
}

wayland::compositor_t CConnection::GetCompositor()
{
  assert(m_compositor);
  return m_compositor;
}

wayland::shell_t CConnection::GetShell()
{
  assert(m_shell);
  return m_shell;
}

wayland::shm_t CConnection::GetShm()
{
  assert(m_shm);
  return m_shm;
}

wayland::data_device_manager_t CConnection::GetDataDeviceManager()
{
  return m_dataDeviceManager;
}

wayland::subcompositor_t CConnection::GetSubcompositor()
{
  return m_subcompositor;
}

wayland::zxdg_shell_v6_t CConnection::GetXdgShellUnstableV6()
{
  return m_xdgShellUnstableV6;
}

wayland::zwp_idle_inhibit_manager_v1_t CConnection::GetIdleInhibitManagerUnstableV1()
{
  return m_idleInhibitManagerUnstableV1;
}

wayland::presentation_t CConnection::GetPresentation()
{
  return m_presentation;
}
