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
#include <limits>

#include <linux/input-event-codes.h>
#include <wayland-client-protocol.hpp>

#include "input/MouseStat.h"
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
: m_globalName(globalName), m_seat(seat), m_handler(handler)
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

void CSeatInputProcessor::HandlePointerCapability()
{
  m_pointer.on_enter() = [this](std::uint32_t serial, wayland::surface_t surface, std::int32_t surfaceX, std::int32_t surfaceY)
  {
    m_handler->OnSetCursor(m_pointer, serial);
    m_handler->OnEnter(m_globalName, InputType::POINTER);
    m_pointerX = wl_fixed_to_int(surfaceX);
    m_pointerY = wl_fixed_to_int(surfaceY);
    SendMouseMotion();
  };
  m_pointer.on_leave() = [this](std::uint32_t serial, wayland::surface_t surface)
  {
    m_handler->OnLeave(m_globalName, InputType::POINTER);
  };
  m_pointer.on_motion() = [this](std::uint32_t time, std::int32_t surfaceX, std::int32_t surfaceY)
  {
    m_pointerX = wl_fixed_to_int(surfaceX);
    m_pointerY = wl_fixed_to_int(surfaceY);
    SendMouseMotion();
  };
  m_pointer.on_button() = [this](std::uint32_t serial, std::uint32_t time, std::uint32_t button, wayland::pointer_button_state state)
  {
    // Keep track of currently pressed buttons, we need that for motion events
    // FIXME Is the state actually used?
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
  m_keyboard.on_keymap() = [this](wayland::keyboard_keymap_format format, int fd, std::uint32_t size)
  {
    if (format != wayland::keyboard_keymap_format::xkb_v1)
    {
      CLog::Log(LOGWARNING, "Wayland compositor sent keymap in format %u, but we only understand xkbv1 - keyboard input will not work", format);
      // File descriptor should always be closed
      close(fd);
      return;
    }
    
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
      CLog::Log(LOGERROR, "Could not parse keymap from Wayland compositor, continuing without: %s", e.what());
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

  SendKey(scancode, xbmcKey, static_cast<std::uint16_t> (utf32), pressed);
}

void CSeatInputProcessor::SendKey(unsigned char scancode, XBMCKey key, std::uint16_t unicodeCodepoint, bool pressed)
{
  assert(m_keymap);

  XBMC_Event event;
  event.type = static_cast<unsigned char> (pressed ? XBMC_KEYDOWN : XBMC_KEYUP);
  event.key.keysym =
  {
    // The scancode is a char, which is not enough to hold the value from Wayland anyway
    .scancode = scancode,
    .sym = key,
    .mod = m_keymap->ActiveXBMCModifiers(),
    .unicode = unicodeCodepoint
  };
  m_handler->OnEvent(m_globalName, InputType::KEYBOARD, event);
}

void CSeatInputProcessor::HandleTouchCapability()
{
}
