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

#include "Output.h"

#include <cassert>
#include <set>

using namespace KODI::WINDOWING::WAYLAND;

COutput::COutput(std::uint32_t globalName, wayland::output_t const & output, std::function<void()> doneHandler)
: m_globalName(globalName), m_output(output), m_doneHandler(doneHandler)
{
  assert(m_output);

  m_output.on_geometry() = [this](std::int32_t x, std::int32_t y, std::int32_t physWidth, std::int32_t physHeight, wayland::output_subpixel subpixel, std::string const& make, std::string const& model, wayland::output_transform transform)
  {
    m_x = x;
    m_y = y;
    m_physicalWidth = physWidth;
    m_physicalHeight = physHeight;
    m_make = make;
    m_model = model;
  };
  m_output.on_mode() = [this](wayland::output_mode flags, std::int32_t width, std::int32_t height, std::int32_t refresh)
  {
    // std::set.emplace returns pair of iterator to the (possibly) inserted
    // element and boolean information whether the element was actually added
    // which we do not need
    auto modeIterator = m_modes.emplace(width, height, refresh).first;
    // Remember current and preferred mode
    // Current mode is the last one that was sent with current flag set
    if (flags & wayland::output_mode::current)
    {
      m_currentMode = modeIterator;
    }
    if (flags & wayland::output_mode::preferred)
    {
      m_preferredMode = modeIterator;
    }
  };
  m_output.on_scale() = [this](std::int32_t scale)
  {
    m_scale = scale;
  };

  m_output.on_done() = [this]()
  {
    m_doneHandler();
  };
}

COutput::~COutput()
{
  // Reset event handlers - someone might still hold a reference to the output_t,
  // causing events to be dispatched. They should not go to a deleted class.
  m_output.on_geometry() = nullptr;
  m_output.on_mode() = nullptr;
  m_output.on_done() = nullptr;
  m_output.on_scale() = nullptr;
}


float COutput::GetPixelRatioForMode(const Mode& mode) const
{
  if (m_physicalWidth == 0 || m_physicalHeight == 0 || mode.width == 0 || mode.height == 0)
  {
    return 1.0f;
  }
  else
  {
    return (
            (static_cast<float> (m_physicalWidth) / static_cast<float> (mode.width))
            /
            (static_cast<float> (m_physicalHeight) / static_cast<float> (mode.height))
            );
  }
}
