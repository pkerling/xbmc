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
#include <set>
#include <stdexcept>
#include <tuple>

#include <wayland-client-protocol.hpp>

namespace KODI
{
namespace WINDOWING
{
namespace WAYLAND
{

/**
 * wl_output handler that collects information from the compositor and then
 * passes it on when everything is available
 */
class COutput
{
public:
  COutput(std::uint32_t globalName, wayland::output_t const & output, std::function<void()> doneHandler);
  COutput(COutput&& other) = default;
  
  wayland::output_t const& GetWaylandOutput() const
  {
    return m_output;
  }
  std::uint32_t GetGlobalName() const
  {
    return m_globalName;
  }
  /**
   * Get output position in compositor coordinate space
   * \return (x, y) tuple of output position
   */
  std::tuple<std::int32_t, std::int32_t> GetPosition() const
  {
    return std::make_tuple(m_x, m_y);
  }
  /**
   * Get output physical size in millimeters
   * \return (width, height) tuple of output physical size in millimeters
   */
  std::tuple<std::int32_t, std::int32_t> GetPhysicalSize() const
  {
    return std::make_tuple(m_physicalWidth, m_physicalHeight);
  }
  std::string const& GetMake() const
  {
    return m_make;
  }
  std::string const& GetModel() const
  {
    return m_model;
  }
  std::int32_t GetScale() const
  {
    return m_scale;
  }
  
  struct Mode
  {
    std::int32_t width, height;
    std::int32_t refreshMilliHz;
    Mode(std::int32_t width, std::int32_t height, std::int32_t refreshMilliHz)
      : width(width), height(height), refreshMilliHz(refreshMilliHz) {}
    
    std::tuple<std::int32_t, std::int32_t, std::int32_t> AsTuple() const
    {
      return std::make_tuple(width, height, refreshMilliHz);
    }
    
    // Comparison operator needed for std::set
    bool operator<(const Mode& right) const
    {
      return AsTuple() < right.AsTuple();
    }
    
    bool operator==(const Mode& right) const
    {
      return AsTuple() == right.AsTuple();
    }
    
    bool operator!=(const Mode& right) const
    {
      return !(*this == right);
    }
  };
  
  std::set<Mode> const& GetModes() const
  {
    return m_modes;
  }
  Mode const& GetCurrentMode() const
  {
    if (m_currentMode == m_modes.end())
    {
      throw std::runtime_error("Current mode not set");
    }
    return *m_currentMode;
  }
  Mode const& GetPreferredMode() const
  {
    if (m_preferredMode == m_modes.end())
    {
      throw std::runtime_error("Preferred mode not set");
    }
    return *m_preferredMode;
  }
  
  float GetPixelRatioForMode(Mode const& mode) const;
  
private:
  COutput(COutput const& other) = delete;
  COutput& operator=(COutput const& other) = delete;
  
  std::uint32_t m_globalName;
  wayland::output_t m_output;
  std::function<void()> m_doneHandler;
  
  std::int32_t m_x = 0, m_y = 0;
  std::int32_t m_physicalWidth = 0, m_physicalHeight = 0;
  std::string m_make, m_model;
  std::int32_t m_scale = 1; // default scale of 1 if no wl_output::scale is sent
  
  std::set<Mode> m_modes;
  // For std::set, insertion never invalidates existing iterators, and modes are
  // never removed, so the usage of iterators is safe
  std::set<Mode>::iterator m_currentMode = m_modes.end();
  std::set<Mode>::iterator m_preferredMode = m_modes.end();
};


}
}
}