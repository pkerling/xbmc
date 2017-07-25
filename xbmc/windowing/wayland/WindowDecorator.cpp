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

#include "WindowDecorator.h"

#include <algorithm>
#include <cassert>
#include <cmath>

#include <linux/input-event-codes.h>
#include <utility>

#include "threads/SingleLock.h"
#include "utils/EndianSwap.h"
#include "utils/log.h"

using namespace KODI::UTILS::POSIX;
using namespace KODI::WINDOWING::WAYLAND;
using namespace std::placeholders;

// FIXME thread safety on destruction

namespace
{

/// Bytes per pixel in shm storage
constexpr int BYTES_PER_PIXEL{4};
/// Width of the border around the whole window
constexpr int BORDER_WIDTH{5};
/// Height of the top bar
constexpr int TOP_BAR_HEIGHT{33};
/// Maximum distance from the window corner to consider position valid for resize
constexpr int RESIZE_MAX_CORNER_DISTANCE{BORDER_WIDTH};
/// Distance of buttons from edges of the top bar
constexpr int BUTTONS_EDGE_DISTANCE{6};
/// Distance from button inner edge to button content
constexpr int BUTTON_INNER_SEPARATION{4};
/// Button size
constexpr int BUTTON_SIZE{21};

constexpr std::uint32_t BORDER_COLOR{0xFF000000u};
constexpr std::uint32_t BUTTON_COLOR_ACTIVE{0xFFFFFFFFu};
constexpr std::uint32_t BUTTON_COLOR_INACTIVE{0xFF777777u};

static_assert(BUTTON_SIZE <= TOP_BAR_HEIGHT - BUTTONS_EDGE_DISTANCE * 2, "Buttons must fit in top bar");

/*
 * Decorations consist of four surfaces, one for each edge of the window. It would
 * also be possible to position one single large surface behind the main surface
 * instead, but that would waste a lot of memory on big/high-density screens.
 *
 * The four surfaces are laid out as follows: Top and bottom surfaces go over the
 * whole width of the main surface plus the left and right borders.
 * Left and right surfaces only go over the height of the main surface without
 * the top and bottom borders.
 *
 * ---------------------------------------------
 * |                   TOP                     |
 * ---------------------------------------------
 * |   |                                   |   |
 * | L |                                   | R |
 * | E |                                   | I |
 * | F |           Main surface            | G |
 * | T |                                   | H |
 * |   |                                   | T |
 * |   |                                   |   |
 * ---------------------------------------------
 * |                 BOTTOM                    |
 * ---------------------------------------------
 */

CSizeInt SurfaceSize(SurfaceIndex type, CSizeInt windowSurfaceSize)
{
  switch (type)
  {
    case SURFACE_TOP:
      return { windowSurfaceSize.Width() + 2 * BORDER_WIDTH, TOP_BAR_HEIGHT + BORDER_WIDTH };
    case SURFACE_RIGHT:
    case SURFACE_LEFT:
      return { BORDER_WIDTH, windowSurfaceSize.Height() };
    case SURFACE_BOTTOM:
      return { windowSurfaceSize.Width() + 2 * BORDER_WIDTH, BORDER_WIDTH };
    default:
      throw std::logic_error("Invalid surface type");
  }
}

/**
 * Full size of decorations to be added to the main surface size
 */
CSizeInt DecorationSize()
{
  return {2 * BORDER_WIDTH, 2 * BORDER_WIDTH + TOP_BAR_HEIGHT};
}

std::size_t MemoryBytesForSize(CSizeInt windowSurfaceSize, int scale)
{
  std::size_t size{};

  for (auto surface : { SURFACE_TOP, SURFACE_RIGHT, SURFACE_BOTTOM, SURFACE_LEFT })
  {
    size += SurfaceSize(surface, windowSurfaceSize).Area();
  }

  size *= scale;

  size *= BYTES_PER_PIXEL;

  return size;
}

std::size_t PositionInBuffer(CWindowDecorator::Buffer& buffer, CPointInt position)
{
  if (position.x < 0 || position.y < 0)
  {
    throw std::invalid_argument("Position out of bounds");
  }
  std::size_t offset = buffer.size.Width() * position.y + position.x;
  if (offset * 4 >= buffer.dataSize)
  {
    throw std::invalid_argument("Position out of bounds");
  }
  return offset;
}

void DrawHorizontalLine(CWindowDecorator::Buffer& buffer, std::uint32_t color, CPointInt position, int length)
{
  auto offsetStart = PositionInBuffer(buffer, position), offsetEnd = PositionInBuffer(buffer, position + CPointInt{length - 1, 0});
  if (offsetEnd < offsetStart)
  {
    throw std::invalid_argument("Invalid drawing coordinates");
  }
  std::fill(buffer.RgbaBuffer() + offsetStart, buffer.RgbaBuffer() + offsetEnd + 1, Endian_SwapLE32(color));
}

void DrawLineWithStride(CWindowDecorator::Buffer& buffer, std::uint32_t color, CPointInt position, int length, int stride)
{
  auto offsetStart = PositionInBuffer(buffer, position), offsetEnd = offsetStart + stride * (length - 1);
  if (offsetEnd * 4 >= buffer.dataSize)
  {
    throw std::invalid_argument("Position out of bounds");
  }
  if (offsetEnd < offsetStart)
  {
    throw std::invalid_argument("Invalid drawing coordinates");
  }
  auto* memory = buffer.RgbaBuffer();
  for (std::size_t offset = offsetStart; offset <= offsetEnd; offset += stride)
  {
    *(memory + offset) = Endian_SwapLE32(color);
  }
}

void DrawVerticalLine(CWindowDecorator::Buffer& buffer, std::uint32_t color, CPointInt position, int length)
{
  DrawLineWithStride(buffer, color, position, length, buffer.size.Width());
}

/**
 * Draw rectangle inside the specified size
 */
void DrawRectangle(CWindowDecorator::Buffer& buffer, std::uint32_t color, CRectInt rect)
{
  DrawHorizontalLine(buffer, color, rect.P1(), rect.Width());
  DrawVerticalLine(buffer, color, rect.P1(), rect.Height());
  DrawHorizontalLine(buffer, color, rect.P1() + CPointInt{1, rect.Height() - 1}, rect.Width() - 1);
  DrawVerticalLine(buffer, color, rect.P1() + CPointInt{rect.Width() - 1, 1}, rect.Height() - 1);
}

wayland::shell_surface_resize ResizeEdgeForPosition(SurfaceIndex surface, CSizeInt surfaceSize, CPointInt position)
{
  switch (surface)
  {
    case SURFACE_TOP:
      if (position.y <= RESIZE_MAX_CORNER_DISTANCE)
      {
        if (position.x <= RESIZE_MAX_CORNER_DISTANCE)
        {
          return wayland::shell_surface_resize::top_left;
        }
        else if (position.x >= surfaceSize.Width() - RESIZE_MAX_CORNER_DISTANCE)
        {
          return wayland::shell_surface_resize::top_right;
        }
        else
        {
          return wayland::shell_surface_resize::top;
        }
      }
      else
      {
        if (position.x <= RESIZE_MAX_CORNER_DISTANCE)
        {
          return wayland::shell_surface_resize::left;
        }
        else if (position.x >= surfaceSize.Width() - RESIZE_MAX_CORNER_DISTANCE)
        {
          return wayland::shell_surface_resize::right;
        }
        else
        {
          // Inside title bar, not resizing
          return wayland::shell_surface_resize::none;
        }
      }
    case SURFACE_RIGHT:
      if (position.y >= surfaceSize.Height() - RESIZE_MAX_CORNER_DISTANCE)
      {
        return wayland::shell_surface_resize::bottom_right;
      }
      else
      {
        return wayland::shell_surface_resize::right;
      }
    case SURFACE_BOTTOM:
      if (position.x <= RESIZE_MAX_CORNER_DISTANCE)
      {
        return wayland::shell_surface_resize::bottom_left;
      }
      else if (position.x >= surfaceSize.Width() - RESIZE_MAX_CORNER_DISTANCE)
      {
        return wayland::shell_surface_resize::bottom_right;
      }
      else
      {
        return wayland::shell_surface_resize::bottom;
      }
    case SURFACE_LEFT:
      if (position.y >= surfaceSize.Height() - RESIZE_MAX_CORNER_DISTANCE)
      {
        return wayland::shell_surface_resize::bottom_left;
      }
      else
      {
        return wayland::shell_surface_resize::left;
      }

    default:
      return wayland::shell_surface_resize::none;
  }
}

/**
 * Get name for resize cursor according to xdg cursor-spec
 */
std::string CursorForResizeEdge(wayland::shell_surface_resize edge)
{
  if (edge == wayland::shell_surface_resize::top)
    return "n-resize";
  else if (edge == wayland::shell_surface_resize::bottom)
    return "s-resize";
  else if (edge == wayland::shell_surface_resize::left)
    return "w-resize";
  else if (edge == wayland::shell_surface_resize::top_left)
    return "nw-resize";
  else if (edge == wayland::shell_surface_resize::bottom_left)
    return "sw-resize";
  else if (edge == wayland::shell_surface_resize::right)
    return "e-resize";
  else if (edge == wayland::shell_surface_resize::top_right)
    return "ne-resize";
  else if (edge == wayland::shell_surface_resize::bottom_right)
    return "se-resize";
  else
    return "";
}

template<typename T, typename InstanceProviderT>
bool HandleCapabilityChange(wayland::seat_capability caps,
                            wayland::seat_capability cap,
                            T& proxy,
                            InstanceProviderT instanceProvider)
{
  bool hasCapability = caps & cap;

  if ((!!proxy) != hasCapability)
  {
    // Capability changed

    if (hasCapability)
    {
      // The capability was added
      proxy = instanceProvider();
      return true;
    }
    else
    {
      // The capability was removed
      proxy.proxy_release();
    }
  }

  return false;
}

}

CWindowDecorator::CWindowDecorator(IWindowDecorationHandler& handler, CConnection& connection, wayland::surface_t const& mainSurface)
: m_handler{handler}, m_registry{connection}, m_mainSurface{mainSurface}, m_buttonColor{BUTTON_COLOR_ACTIVE}
{
  static_assert(std::tuple_size<decltype(m_surfaces)>::value == SURFACE_COUNT, "SURFACE_COUNT must match surfaces array size");

  m_registry.RequestSingleton(m_compositor, 1, 4);
  m_registry.RequestSingleton(m_subcompositor, 1, 1, false);
  m_registry.RequestSingleton(m_shm, 1, 1);
  m_registry.Request<wayland::seat_t>(1, 5, std::bind(&CWindowDecorator::OnSeatAdded, this, _1, _2), std::bind(&CWindowDecorator::OnSeatRemoved, this, _1));

  m_registry.Bind();
}

void CWindowDecorator::PositionButtons()
{
  CPointInt position{m_surfaces[SURFACE_TOP].currentBuffer.size.Width() - BORDER_WIDTH, BORDER_WIDTH + BUTTONS_EDGE_DISTANCE};
  for (auto iter = m_buttons.rbegin(); iter != m_buttons.rend(); iter++)
  {
    position.x -= (BUTTONS_EDGE_DISTANCE + BUTTON_SIZE);
    // Clamp if not enough space
    position.x = std::max(0, position.x);

    iter->position = CRectInt{position, position + CPointInt{BUTTON_SIZE, BUTTON_SIZE}};
  }
}

void CWindowDecorator::OnSeatAdded(std::uint32_t name, wayland::proxy_t&& proxy)
{
  wayland::seat_t seat{proxy};
  seat.on_capabilities() = std::bind(&CWindowDecorator::OnSeatCapabilities, this, name, _1);
  m_seats.emplace(name, std::move(seat));
}

void CWindowDecorator::OnSeatRemoved(std::uint32_t name)
{
  m_seats.erase(name);
}

void CWindowDecorator::OnSeatCapabilities(std::uint32_t name, wayland::seat_capability capabilities)
{
  auto& seat = m_seats.at(name);
  if (HandleCapabilityChange(capabilities, wayland::seat_capability::pointer, seat.pointer, std::bind(&wayland::seat_t::get_pointer, seat.seat)))
  {
    HandleSeatPointer(seat);
  }
  if (HandleCapabilityChange(capabilities, wayland::seat_capability::touch, seat.touch, std::bind(&wayland::seat_t::get_touch, seat.seat)))
  {
    HandleSeatTouch(seat);
  }
}

void CWindowDecorator::HandleSeatPointer(Seat& seat)
{
  seat.pointer.on_enter() = [this,&seat](std::uint32_t serial, wayland::surface_t surface, float x, float y)
  {
    // Reset first so we ignore events for surfaces we don't handle
   seat.currentSurface = SURFACE_COUNT;
   CSingleLock lock(m_mutex);
   for (std::size_t i = 0; i < m_surfaces.size(); i++)
   {
     if (m_surfaces[i].surface == surface)
      {
       seat.pointerEnterSerial = serial;
       seat.currentSurface = static_cast<SurfaceIndex> (i);
       seat.pointerPosition = {x, y};
       UpdateSeatCursor(seat);
       break;
      }
    }
  };
  seat.pointer.on_leave() = [&seat](std::uint32_t, wayland::surface_t)
  {
    seat.currentSurface = SURFACE_COUNT;
    // Recreate cursor surface on reenter
    seat.cursorName.clear();
    seat.cursor.proxy_release();
  };
  seat.pointer.on_motion() = [this, &seat](std::uint32_t, float x, float y)
  {
    if (seat.currentSurface != SURFACE_COUNT)
    {
      seat.pointerPosition = {x, y};
      UpdateSeatCursor(seat);
    }
  };
  seat.pointer.on_button() = [this, &seat](std::uint32_t serial, std::uint32_t, std::uint32_t button, wayland::pointer_button_state state)
  {
    if (seat.currentSurface != SURFACE_COUNT && state == wayland::pointer_button_state::pressed)
    {
      HandleSeatClick(seat.seat, seat.currentSurface, serial, button, seat.pointerPosition);
    }
  };
}

void CWindowDecorator::HandleSeatTouch(Seat& seat)
{
  seat.touch.on_down() = [this, &seat](std::uint32_t serial, std::uint32_t, wayland::surface_t surface, std::int32_t id, float x, float y)
  {
   CSingleLock lock(m_mutex);
   for (std::size_t i = 0; i < m_surfaces.size(); i++)
   {
     if (m_surfaces[i].surface == surface)
     {
       HandleSeatClick(seat.seat, static_cast<SurfaceIndex> (i), serial, BTN_LEFT, {x, y});
     }
   }
  };
}

void CWindowDecorator::UpdateSeatCursor(Seat& seat)
{
  if (seat.currentSurface == SURFACE_COUNT)
  {
    // Don't set anything if not on any surface
    return;
  }

  LoadCursorTheme();

  std::string cursorName{"default"};

  {
    CSingleLock lock(m_mutex);
    auto resizeEdge = ResizeEdgeForPosition(seat.currentSurface, SurfaceSize(seat.currentSurface, m_mainSurfaceSize), CPointInt{seat.pointerPosition});
    if (resizeEdge != wayland::shell_surface_resize::none)
    {
      cursorName = CursorForResizeEdge(resizeEdge);
    }
  }

  if (cursorName == seat.cursorName)
  {
    // Don't reload cursor all the time when nothing is changing
    return;
  }
  seat.cursorName = cursorName;

  wayland::cursor_t cursor;
  try
  {
    cursor = m_cursorTheme.get_cursor(cursorName);
  }
  catch (std::exception& e)
  {
    CLog::LogF(LOGERROR, "Could not get required cursor %s from cursor theme: %s", cursorName.c_str(), e.what());
    return;
  }
  auto cursorImage = cursor.image(0);

  if (!seat.cursor)
  {
    seat.cursor = m_compositor.create_surface();
  }

  seat.pointer.set_cursor(seat.pointerEnterSerial, seat.cursor, cursorImage.hotspot_x(), cursorImage.hotspot_y());
  seat.cursor.attach(cursorImage.get_buffer(), 0, 0);
  seat.cursor.damage(0, 0, cursorImage.width(), cursorImage.height());
  if (seat.cursor.can_set_buffer_scale())
  {
    seat.cursor.set_buffer_scale(m_scale);
  }
  seat.cursor.commit();
}


void CWindowDecorator::HandleSeatClick(wayland::seat_t seat, SurfaceIndex surface, std::uint32_t serial, std::uint32_t button, CPoint position)
{
  switch (button)
  {
    case BTN_LEFT:
    {
      CSingleLock lock(m_mutex);
      auto resizeEdge = ResizeEdgeForPosition(surface, m_mainSurfaceSize, CPointInt{position});
      if (resizeEdge == wayland::shell_surface_resize::none)
      {
        for (auto const& button : m_buttons)
        {
          if (button.position.PtInRect(CPointInt{position}))
          {
            button.onClick();
            return;
          }
        }

        m_handler.OnWindowMove(seat, serial);
      }
      else
      {
        m_handler.OnWindowResize(seat, serial, resizeEdge);
      }
    }
    break;
    case BTN_RIGHT:
      if (surface == SURFACE_TOP)
      {
        m_handler.OnWindowShowContextMenu(seat, serial, CPointInt{position} - CPointInt{BORDER_WIDTH, BORDER_WIDTH + TOP_BAR_HEIGHT});
      }
      break;
  }
}

CWindowDecorator::BorderSurface CWindowDecorator::MakeBorderSurface()
{
  auto surface = m_compositor.create_surface();
  auto subsurface = m_subcompositor.get_subsurface(surface, m_mainSurface);
  return {surface, subsurface};
}

bool CWindowDecorator::IsDecorationActive() const
{
  return StateHasWindowDecorations(m_windowState);
}

bool CWindowDecorator::StateHasWindowDecorations(IShellSurface::StateBitset state) const
{
  // No decorations possible if subcompositor not available
  return m_subcompositor && !state.test(IShellSurface::STATE_FULLSCREEN);
}

CSizeInt CWindowDecorator::CalculateMainSurfaceSize(CSizeInt size, IShellSurface::StateBitset state)
{
  if (StateHasWindowDecorations(state))
  {
    // Subtract decorations
    return size - DecorationSize();
  }
  else
  {
    // Fullscreen -> no decorations
    return size;
  }
}

CSizeInt CWindowDecorator::CalculateFullSurfaceSize(CSizeInt size, IShellSurface::StateBitset state)
{
  if (StateHasWindowDecorations(state))
  {
    // Add decorations
    return size + DecorationSize();
  }
  else
  {
    // Fullscreen -> no decorations
    return size;
  }
}

void CWindowDecorator::SetState(CSizeInt size, int scale, IShellSurface::StateBitset state)
{
  CSizeInt mainSurfaceSize = CalculateMainSurfaceSize(size, state);
  if (mainSurfaceSize == m_mainSurfaceSize && scale == m_scale && state == m_windowState)
  {
    return;
  }

  bool wasDecorations = IsDecorationActive();
  m_windowState = state;

  m_buttonColor = m_windowState.test(IShellSurface::STATE_ACTIVATED) ? BUTTON_COLOR_ACTIVE : BUTTON_COLOR_INACTIVE;

  CLog::Log(LOGDEBUG, "CWindowDecorator::SetState: Setting full surface size %dx%d scale %d (main surface size %dx%d), decorations active: %u", size.Width(), size.Height(), scale, mainSurfaceSize.Width(), mainSurfaceSize.Height(), IsDecorationActive());

  if (mainSurfaceSize != m_mainSurfaceSize || scale != m_scale || wasDecorations != IsDecorationActive())
  {
    if (scale != m_scale)
    {
      // Reload cursor theme
      CLog::Log(LOGDEBUG, "CWindowDecorator::SetState: Buffer scale changed, reloading cursor theme");
      m_cursorTheme = wayland::cursor_theme_t();
      for (auto& seat : m_seats)
      {
        UpdateSeatCursor(seat.second);
      }
    }

    m_mainSurfaceSize = mainSurfaceSize;
    m_scale = scale;
    CLog::Log(LOGDEBUG, "CWindowDecorator::SetState: Resetting decorations");
    Reset();
  }
  else if (IsDecorationActive())
  {
    CLog::Log(LOGDEBUG, "CWindowDecorator::SetState: Repainting decorations");
    // Only state differs, no reallocation needed
    Repaint();
  }
}

void CWindowDecorator::Reset()
{
  ResetButtons();
  ResetSurfaces();
  ResetShm();
  if (IsDecorationActive())
  {
    ReattachSubsurfaces();
    AllocateBuffers();
    PositionButtons();
    Repaint();
  }
}

void CWindowDecorator::ResetButtons()
{
  CSingleLock lock(m_mutex);

  if (IsDecorationActive())
  {
    if (m_buttons.empty())
    {
      // Minimize
      m_buttons.emplace_back();
      Button& minimize = m_buttons.back();
      minimize.draw = [this](Buffer& buffer, CRectInt position)
      {
        DrawRectangle(buffer, m_buttonColor, position);
        DrawHorizontalLine(buffer, m_buttonColor, position.P1() + CPointInt{BUTTON_INNER_SEPARATION, position.Height() - BUTTON_INNER_SEPARATION - 1}, position.Width() - 2 * BUTTON_INNER_SEPARATION);
      };
      minimize.onClick = [this] { m_handler.OnWindowMinimize(); };

      // Maximize
      m_buttons.emplace_back();
      Button& maximize = m_buttons.back();
      maximize.draw = [this](Buffer& buffer, CRectInt position)
      {
        DrawRectangle(buffer, m_buttonColor, position);
        DrawRectangle(buffer, m_buttonColor, {position.P1() + CPointInt{BUTTON_INNER_SEPARATION, BUTTON_INNER_SEPARATION}, position.P2() - CPointInt{BUTTON_INNER_SEPARATION, BUTTON_INNER_SEPARATION}});
        DrawHorizontalLine(buffer, m_buttonColor, position.P1() + CPointInt{BUTTON_INNER_SEPARATION, BUTTON_INNER_SEPARATION + 1}, position.Width() - 2 * BUTTON_INNER_SEPARATION);
      };
      maximize.onClick = [this] { m_handler.OnWindowMaximize(); };

      // Close
      m_buttons.emplace_back();
      Button& close = m_buttons.back();
      close.draw = [this](Buffer& buffer, CRectInt position)
      {
        DrawRectangle(buffer, m_buttonColor, position);
        auto diagonal = position.Width() - 2 * BUTTON_INNER_SEPARATION;
        DrawLineWithStride(buffer, m_buttonColor, position.P1() + CPointInt{BUTTON_INNER_SEPARATION, BUTTON_INNER_SEPARATION}, diagonal, buffer.size.Width() + 1);
        DrawLineWithStride(buffer, m_buttonColor, position.P1() + CPointInt{position.Width() - BUTTON_INNER_SEPARATION - 1, BUTTON_INNER_SEPARATION}, diagonal, buffer.size.Width() - 1);
      };
      close.onClick = [this] { m_handler.OnWindowClose(); };
    }
  }
  else
  {
    m_buttons.clear();
  }
}

void CWindowDecorator::ResetSurfaces()
{
  CSingleLock lock(m_mutex);
  if (IsDecorationActive())
  {
    if (!m_surfaces.front().surface)
    {
      std::generate(m_surfaces.begin(), m_surfaces.end(), std::bind(&CWindowDecorator::MakeBorderSurface, this));
    }
  }
  else
  {
    for (auto& surface : m_surfaces)
    {
      if (surface.surface)
      {
        // Destroying the surface would cause some flicker because it takes effect
        // immediately, before the next commit on the main surface - just make it
        // invisible by attaching a NULL buffer
        surface.surface.attach(wayland::buffer_t(), 0, 0);
        surface.surface.commit();
      }
    }
  }
}

void CWindowDecorator::ReattachSubsurfaces()
{
  CSingleLock lock(m_mutex);
  m_surfaces[SURFACE_TOP].subsurface.set_position(-BORDER_WIDTH, -(BORDER_WIDTH + TOP_BAR_HEIGHT));
  m_surfaces[SURFACE_RIGHT].subsurface.set_position(m_mainSurfaceSize.Width(), 0);
  m_surfaces[SURFACE_BOTTOM].subsurface.set_position(-BORDER_WIDTH, m_mainSurfaceSize.Height());
  m_surfaces[SURFACE_LEFT].subsurface.set_position(-BORDER_WIDTH, 0);
}

void CWindowDecorator::ResetShm()
{
  CSingleLock lock(m_mutex);
  if (IsDecorationActive())
  {
    m_memory.reset(new CSharedMemory(MemoryBytesForSize(m_mainSurfaceSize, m_scale)));
    m_memoryAllocatedSize = 0;
    m_shmPool = m_shm.create_pool(m_memory->Fd(), m_memory->Size());
  }
  else
  {
    m_memory.reset();
    m_shmPool.proxy_release();
  }

  for (auto& surface : m_surfaces)
  {
    surface.currentBuffer.data = nullptr;
  }
}

CWindowDecorator::Buffer CWindowDecorator::GetBuffer(CSizeInt size)
{
  // We ignore tearing on the decorations for now.
  // We can always implement a clever buffer management scheme later... :-)

  auto totalSize{size.Area() * BYTES_PER_PIXEL};
  if (m_memory->Size() < m_memoryAllocatedSize + totalSize)
  {
    // We miscalculated something
    throw std::logic_error("Remaining SHM pool size is too small for requested buffer");
  }
  // argb8888 support is mandatory
  auto buffer = m_shmPool.create_buffer(m_memoryAllocatedSize, size.Width(), size.Height(), size.Width() * BYTES_PER_PIXEL, wayland::shm_format::argb8888);

  void* data = static_cast<std::uint8_t*> (m_memory->Data()) + m_memoryAllocatedSize;
  m_memoryAllocatedSize += totalSize;

  return { data, totalSize, size, std::move(buffer) };
}

void CWindowDecorator::AllocateBuffers()
{
  CSingleLock lock(m_mutex);
  for (std::size_t i = 0; i < m_surfaces.size(); i++)
  {
    if (!m_surfaces[i].currentBuffer.data)
    {
      auto size = SurfaceSize(static_cast<SurfaceIndex> (i), m_mainSurfaceSize);
      m_surfaces[i].currentBuffer = GetBuffer(size * m_scale);
      auto region = m_compositor.create_region();
      region.add(0, 0, size.Width(), size.Height());
      m_surfaces[i].surface.set_opaque_region(region);
      if (m_surfaces[i].surface.can_set_buffer_scale())
      {
        m_surfaces[i].surface.set_buffer_scale(m_scale);
      }
    }
  }
}

void CWindowDecorator::Repaint()
{
  // Fill opaque black
  for (auto& surface : m_surfaces)
  {
    std::fill_n(static_cast<std::uint32_t*> (surface.currentBuffer.data), surface.currentBuffer.size.Area(), Endian_SwapLE32(BORDER_COLOR));
  }
  auto& topBuffer = m_surfaces[SURFACE_TOP].currentBuffer;
  auto innerBorderColor = m_buttonColor;
  // Draw rectangle
  DrawHorizontalLine(topBuffer, innerBorderColor, {BORDER_WIDTH - 1, BORDER_WIDTH - 1}, topBuffer.size.Width() - 2 * BORDER_WIDTH + 2);
  DrawVerticalLine(topBuffer, innerBorderColor, {BORDER_WIDTH - 1, BORDER_WIDTH - 1}, topBuffer.size.Height() - BORDER_WIDTH + 1);
  DrawVerticalLine(topBuffer, innerBorderColor, {topBuffer.size.Width() - BORDER_WIDTH, BORDER_WIDTH - 1}, topBuffer.size.Height() - BORDER_WIDTH + 1);
  DrawVerticalLine(m_surfaces[SURFACE_LEFT].currentBuffer, innerBorderColor, {BORDER_WIDTH - 1, 0}, m_surfaces[SURFACE_LEFT].currentBuffer.size.Height());
  DrawVerticalLine(m_surfaces[SURFACE_RIGHT].currentBuffer, innerBorderColor, {0, 0}, m_surfaces[SURFACE_RIGHT].currentBuffer.size.Height());
  DrawHorizontalLine(m_surfaces[SURFACE_BOTTOM].currentBuffer, innerBorderColor, {BORDER_WIDTH - 1, 0}, m_surfaces[SURFACE_BOTTOM].currentBuffer.size.Width() - 2 * BORDER_WIDTH + 2);
  // Draw white line into top bar as separator
  DrawHorizontalLine(topBuffer, innerBorderColor, {BORDER_WIDTH - 1, topBuffer.size.Height() - 1}, topBuffer.size.Width() - 2 * BORDER_WIDTH + 2);
  // Draw buttons
  for (auto& button : m_buttons)
  {
    button.draw(topBuffer, button.position);
  }

  // Finally make everything visible
  CommitAllBuffers();
}

void CWindowDecorator::CommitAllBuffers()
{
  CSingleLock lock(m_pendingBuffersMutex);

  for (auto& surface : m_surfaces)
  {
    // Keep buffers in list so they are kept alive even when the Buffers gets
    // recreated on size change
    auto emplaceResult = m_pendingBuffers.emplace(surface.currentBuffer.buffer);
    if (emplaceResult.second)
    {
      // Buffer was not pending already
      auto iter = emplaceResult.first;
      surface.currentBuffer.buffer.on_release() = [this, iter]()
      {
        CSingleLock lock(m_pendingBuffersMutex);
        // Do not erase again until buffer is reattached (should not happen anyway, just to be safe)
        // const_cast is OK since changing the function pointer does not affect
        // the key in the set
        const_cast<wayland::buffer_t&>(*iter).on_release() = nullptr;
        m_pendingBuffers.erase(iter);
      };
    }

    surface.surface.attach(surface.currentBuffer.buffer, 0, 0);
    surface.surface.damage(0, 0, surface.currentBuffer.size.Width(), surface.currentBuffer.size.Height());
    surface.surface.commit();
  }
}

void CWindowDecorator::LoadCursorTheme()
{
  CSingleLock lock(m_mutex);
  if (!m_cursorTheme)
  {
    // Load default cursor theme
    // Base size of 16px is somewhat random
    m_cursorTheme = wayland::cursor_theme_t("", 16 * m_scale, m_shm);
  }
}
