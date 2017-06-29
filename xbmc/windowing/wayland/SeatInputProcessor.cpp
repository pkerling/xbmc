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

#include <unistd.h>

#include <cassert>
#include <cmath>
#include <limits>

#include <linux/input-event-codes.h>
#include <wayland-client-protocol.hpp>

#include "input/MouseStat.h"
#include "input/touch/generic/GenericTouchInputHandler.h"
#include "SeatInputProcessor.h"
#include "utils/log.h"

using namespace KODI::WINDOWING::WAYLAND;

namespace
{

/**
 * Handle change of availability of a wl_seat input capability
 * 
 * This checks whether the capability is currently available with the wl_seat
 * and whether it was bound to an instance. If there is a mismatch between
 * these two, the instance is destroyed if a capability was removed or created
 * if a capability was added.
 * 
 * \param handler CSeatInputProcessor instance
 * \param caps new capabilities
 * \param cap capability to check for
 * \param capName human-readable name of the capability for log messages
 * \param instance reference to the Wayland protocol instance that holds the
 *                 protocol corresponding to the capability
 * \param instanceProvider function that functions as factory for the Wayland
 *                         protocol instance if the capability has been added
 * \param onNewCapability function that is called after setting the new capability
 *                        instance when it was added
 */
template<typename T>
void HandleCapabilityChange(CSeatInputProcessor* handler,
                            wayland::seat_capability caps,
                            wayland::seat_capability cap,
                            std::string const & capName, T& instance,
                            std::function<T()> const & instanceProvider,
                            std::function<void()> const & onNewCapability = std::function<void()>())
{
  bool hasCapability = caps & cap;

  if (instance.proxy_has_object() != hasCapability)
  {
    // Capability changed

    if (hasCapability)
    {
      // The capability was added
      CLog::Log(LOGDEBUG, "Wayland seat %s gained capability %s", handler->GetName().c_str(), capName.c_str());
      instance = instanceProvider();
      onNewCapability();
    }
    else
    {
      // The capability was removed
      CLog::Log(LOGDEBUG, "Wayland seat %s lost capability %s", handler->GetName().c_str(), capName.c_str());
      instance.proxy_release();
    }
  }
};

int WaylandToXbmcButton(std::uint32_t button)
{
  // Wayland button is evdev code
  switch (button)
  {
    case BTN_LEFT:
      return XBMC_BUTTON_LEFT;
    case BTN_MIDDLE:
      return XBMC_BUTTON_MIDDLE;
    case BTN_RIGHT:
      return XBMC_BUTTON_RIGHT;
    default:
      return -1;
  }
}

// Offset between keyboard codes of Wayland (effectively evdev) and xkb_keycode_t
constexpr int WL_KEYBOARD_XKB_CODE_OFFSET = 8;

}

CSeatInputProcessor::CSeatInputProcessor(std::uint32_t globalName, const wayland::seat_t& seat, IInputHandler* handler)
: m_globalName(globalName), m_seat(seat), m_handler(handler), m_keyRepeatCallback(this), m_keyRepeatTimer(&m_keyRepeatCallback)
{
  assert(m_handler);

  m_seat.on_name() = [this](std::string name)
  {
    m_name = name;
  };
  m_seat.on_capabilities() = std::bind(&CSeatInputProcessor::HandleOnCapabilities, this, std::placeholders::_1);
}

void CSeatInputProcessor::HandleOnCapabilities(wayland::seat_capability caps)
{
  HandleCapabilityChange(this,
                         caps,
                         wayland::seat_capability::pointer,
                         "pointer",
                         m_pointer,
                         static_cast<std::function < wayland::pointer_t()>> (std::bind(&wayland::seat_t::get_pointer, &m_seat)),
                         std::bind(&CSeatInputProcessor::HandlePointerCapability, this));
  HandleCapabilityChange(this,
                         caps,
                         wayland::seat_capability::keyboard,
                         "keyboard",
                         m_keyboard,
                         static_cast<std::function < wayland::keyboard_t()>> (std::bind(&wayland::seat_t::get_keyboard, &m_seat)),
                         std::bind(&CSeatInputProcessor::HandleKeyboardCapability, this));
  HandleCapabilityChange(this,
                         caps,
                         wayland::seat_capability::touch,
                         "touch",
                         m_touch,
                         static_cast<std::function < wayland::touch_t()>> (std::bind(&wayland::seat_t::get_touch, &m_seat)),
                         std::bind(&CSeatInputProcessor::HandleTouchCapability, this));
}

std::uint16_t CSeatInputProcessor::ConvertMouseCoordinate(double coord)
{
  return static_cast<std::uint16_t> (std::round(coord * m_coordinateScale));
}

void CSeatInputProcessor::SetMousePosFromSurface(double x, double y)
{
  m_pointerX = ConvertMouseCoordinate(x);
  m_pointerY = ConvertMouseCoordinate(y);
}

void CSeatInputProcessor::HandlePointerCapability()
{
  m_pointer.on_enter() = [this](std::uint32_t serial, wayland::surface_t surface, double surfaceX, double surfaceY)
  {
    m_handler->OnSetCursor(m_pointer, serial);
    m_handler->OnEnter(m_globalName, InputType::POINTER);
    SetMousePosFromSurface(surfaceX, surfaceY);
    SendMouseMotion();
  };
  m_pointer.on_leave() = [this](std::uint32_t serial, wayland::surface_t surface)
  {
    m_handler->OnLeave(m_globalName, InputType::POINTER);
  };
  m_pointer.on_motion() = [this](std::uint32_t time, double surfaceX, double surfaceY)
  {
    SetMousePosFromSurface(surfaceX, surfaceY);
    SendMouseMotion();
  };
  m_pointer.on_button() = [this](std::uint32_t serial, std::uint32_t time, std::uint32_t button, wayland::pointer_button_state state)
  {
    int xbmcButton = WaylandToXbmcButton(button);
    if (xbmcButton < 0)
    {
      // Button is unmapped
      return;
    }

    bool pressed = (state == wayland::pointer_button_state::pressed);
    SendMouseButton(xbmcButton, pressed);
  };
  m_pointer.on_axis() = [this](std::uint32_t serial, wayland::pointer_axis axis, std::int32_t value)
  {
    // For axis events we only care about the vector direction
    // and not the scalar magnitude. Every axis event callback
    // generates one scroll button event for XBMC

    // Negative is up
    unsigned char xbmcButton = (wl_fixed_to_double(value) < 0.0) ? XBMC_BUTTON_WHEELUP : XBMC_BUTTON_WHEELDOWN;
    // Simulate a single click of the wheel-equivalent "button"
    SendMouseButton(xbmcButton, true);
    SendMouseButton(xbmcButton, false);
  };

  // Wayland groups pointer events, but right now there is no benefit in
  // treating them in groups. The main use case for doing so seems to be 
  // multi-axis (i.e. diagnoal) scrolling, but we do not support this anyway.
  /*m_pointer.on_frame() = [this]()
  {
    
  };*/
}

void CSeatInputProcessor::SendMouseMotion()
{
  XBMC_Event event;
  event.type = XBMC_MOUSEMOTION;
  event.motion =
  {
    .x = m_pointerX,
    .y = m_pointerY
  };
  m_handler->OnEvent(m_globalName, InputType::POINTER, event);
}

void CSeatInputProcessor::SendMouseButton(unsigned char button, bool pressed)
{
  XBMC_Event event;
  event.type = static_cast<unsigned char> (pressed ? XBMC_MOUSEBUTTONDOWN : XBMC_MOUSEBUTTONUP);
  event.button =
  {
    .button = button,
    .x = m_pointerX,
    .y = m_pointerY
  };
  m_handler->OnEvent(m_globalName, InputType::POINTER, event);
}

void CSeatInputProcessor::HandleKeyboardCapability()
{
  m_keyboard.on_enter() = [this](std::uint32_t serial, wayland::surface_t surface, wayland::array_t keys)
  {
    m_handler->OnEnter(m_globalName, InputType::KEYBOARD);
  };
  m_keyboard.on_leave() = [this](std::uint32_t serial, wayland::surface_t surface)
  {
    m_handler->OnLeave(m_globalName, InputType::KEYBOARD);
  };
  m_keyboard.on_repeat_info() = [this](std::int32_t rate, std::int32_t delay)
  {
    CLog::Log(LOGDEBUG, "Seat %s key repeat rate: %d cps, delay %d ms", m_name.c_str(), rate, delay);
    // rate is in characters per second, so convert to msec interval
    m_keyRepeatInterval = (rate != 0) ? static_cast<int> (1000.0f / rate) : 0;
    m_keyRepeatDelay = delay;
  };
  m_keyboard.on_keymap() = [this](wayland::keyboard_keymap_format format, int fd, std::uint32_t size)
  {
    if (format != wayland::keyboard_keymap_format::xkb_v1)
    {
      CLog::Log(LOGWARNING, "Wayland compositor sent keymap in format %u, but we only understand xkbv1 - keyboard input will not work", format);
      // File descriptor should always be closed
      close(fd);
      return;
    }
    
    m_keyRepeatTimer.Stop();
    
    try
    {
      if (!m_xkbContext)
      {
        // Lazily initialize XkbcommonContext
        m_xkbContext.reset(new CXkbcommonContext);
      }

      m_keymap.reset(m_xkbContext->KeymapFromSharedMemory(fd, size));
    }
    catch(std::exception& e)
    {
      CLog::Log(LOGERROR, "Could not parse keymap from compositor: %s - continuing without keymap", e.what());
    }
    
    close(fd);
  };
  m_keyboard.on_key() = [this](std::uint32_t serial, std::uint32_t time, std::uint32_t key, wayland::keyboard_key_state state)
  {
    if (!m_keymap)
    {
      CLog::Log(LOGWARNING, "Key event for code %u without valid keymap, ignoring", key);
      return;
    }
    
    ConvertAndSendKey(key, state == wayland::keyboard_key_state::pressed);
  };
  m_keyboard.on_modifiers() = [this](std::uint32_t serial, std::uint32_t modsDepressed, std::uint32_t modsLatched, std::uint32_t modsLocked, std::uint32_t group)
  {
    if (!m_keymap)
    {
      CLog::Log(LOGWARNING, "Modifier event without valid keymap, ignoring");
      return;
    }
    
    m_keyRepeatTimer.Stop();
    m_keymap->UpdateMask(modsDepressed, modsLatched, modsLocked, group);
  };
}

void CSeatInputProcessor::ConvertAndSendKey(std::uint32_t scancode, bool pressed)
{
  std::uint32_t xkbCode = scancode + WL_KEYBOARD_XKB_CODE_OFFSET;
  XBMCKey xbmcKey = m_keymap->XBMCKeyForKeycode(xkbCode);
  std::uint32_t utf32 = m_keymap->UnicodeCodepointForKeycode(xkbCode);
  
  if (utf32 > std::numeric_limits<std::uint16_t>::max())
  {
    // Kodi event system only supports UTF16, so ignore the codepoint if
    // it does not fit
    utf32 = 0;
  }
  if (scancode > std::numeric_limits<unsigned char>::max())
  {
    // Kodi scancodes are limited to unsigned char, pretend the scancode is unknown
    // on overflow
    scancode = 0;
  }

  XBMC_Event event = SendKey(scancode, xbmcKey, static_cast<std::uint16_t> (utf32), pressed);
  
  if (pressed && m_keymap->ShouldKeycodeRepeat(xkbCode) && m_keyRepeatInterval > 0)
  {
    // Can't modify keyToRepeat until we're sure the thread isn't accessing it
    m_keyRepeatTimer.Stop(true);
    // Update/Set key
    m_keyToRepeat = event;
    // Start timer with initial delay
    m_keyRepeatTimer.Start(m_keyRepeatDelay, false);
  }
  else
  {
    m_keyRepeatTimer.Stop();
  }
}

XBMC_Event CSeatInputProcessor::SendKey(unsigned char scancode, XBMCKey key, std::uint16_t unicodeCodepoint, bool pressed)
{
  assert(m_keymap);

  XBMC_Event event;
  event.type = static_cast<unsigned char> (pressed ? XBMC_KEYDOWN : XBMC_KEYUP);
  event.key.keysym =
  {
    .scancode = scancode,
    .sym = key,
    .mod = m_keymap->ActiveXBMCModifiers(),
    .unicode = unicodeCodepoint
  };
  m_handler->OnEvent(m_globalName, InputType::KEYBOARD, event);
  // Return created event for convenience (key repeat)
  return event;
}

CSeatInputProcessor::CKeyRepeatCallback::CKeyRepeatCallback(CSeatInputProcessor* processor)
: m_processor(processor)
{
}

void CSeatInputProcessor::CKeyRepeatCallback::OnTimeout()
{
  // Reset ourselves
  m_processor->m_keyRepeatTimer.RestartAsync(m_processor->m_keyRepeatInterval);
  // Simulate repeat: Key up and down
  XBMC_Event event = m_processor->m_keyToRepeat;
  event.type = XBMC_KEYUP;
  m_processor->m_handler->OnEvent(m_processor->m_globalName, InputType::KEYBOARD, event);
  event.type = XBMC_KEYDOWN;
  m_processor->m_handler->OnEvent(m_processor->m_globalName, InputType::KEYBOARD, event);
}

void CSeatInputProcessor::HandleTouchCapability()
{
  m_touch.on_down() = [this](std::uint32_t serial, std::uint32_t time, wayland::surface_t surface, std::int32_t id, double x, double y)
  {
    // Find free Kodi pointer number
    int kodiPointer = -1;
    // Not optimal, but irrelevant for the small number of iterations
    for (int testPointer = 0; testPointer < TOUCH_MAX_POINTERS; testPointer++)
    {
      if (std::all_of(m_touchPoints.cbegin(), m_touchPoints.cend(),
                      [=](decltype(m_touchPoints)::value_type const& pair)
                      {
                        return (pair.second.kodiPointerNumber != testPointer);
                      }))
      {
        kodiPointer = testPointer;
        break;
      }
    }

    if (kodiPointer != -1)
    {
      auto it = m_touchPoints.emplace(std::piecewise_construct, std::forward_as_tuple(id), std::forward_as_tuple(time, kodiPointer, x * m_coordinateScale, y * m_coordinateScale, 0.0f)).first;
      SendTouchPointEvent(TouchInputDown, it->second);
    }
  };
  m_touch.on_up() = [this](std::uint32_t serial, std::uint32_t time, std::int32_t id)
  {
    auto it = m_touchPoints.find(id);
    if (it != m_touchPoints.end())
    {
      auto& point = it->second;
      point.lastEventTime = time;
      SendTouchPointEvent(TouchInputUp, point);
      m_touchPoints.erase(it);
    }
  };
  m_touch.on_motion() = [this](std::uint32_t time, std::int32_t id, double x, double y)
  {
    auto it = m_touchPoints.find(id);
    if (it != m_touchPoints.end())
    {
      auto& point = it->second;
      point.x = x * m_coordinateScale;
      point.y = y * m_coordinateScale;
      point.lastEventTime = time;
      SendTouchPointEvent(TouchInputMove, point);
    }
  };
  m_touch.on_cancel() = [this]()
  {
    // TouchInputAbort aborts for all pointers, so it does not matter which is specified
    if (!m_touchPoints.empty())
    {
      SendTouchPointEvent(TouchInputAbort, m_touchPoints.begin()->second);
    }
    m_touchPoints.clear();
  };
  m_touch.on_shape() = [this](std::int32_t id, double major, double minor)
  {
    auto it = m_touchPoints.find(id);
    if (it != m_touchPoints.end())
    {
      auto& point = it->second;
      // Kodi only supports size without shape, so use average of both axes
      point.size = ((major + minor) / 2.0) * m_coordinateScale;
      UpdateTouchPoint(point);
    }
  };
}

void CSeatInputProcessor::SendTouchPointEvent(TouchInput event, const TouchPoint& point)
{
  if (event == TouchInputMove)
  {
    for (auto const& point : m_touchPoints)
    {
      // Contrary to the docs, this must be called before HandleTouchInput or the
      // position will not be updated and gesture detection will not work
      UpdateTouchPoint(point.second);
    }
  }
  CGenericTouchInputHandler::GetInstance().HandleTouchInput(event, point.x, point.y, point.lastEventTime * INT64_C(1000000), point.kodiPointerNumber, point.size);
}

void CSeatInputProcessor::UpdateTouchPoint(const TouchPoint& point)
{
  CGenericTouchInputHandler::GetInstance().UpdateTouchPointer(point.kodiPointerNumber, point.x, point.y, point.lastEventTime * INT64_C(1000000), point.size);
}
