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

#include <memory>

#include <wayland-client-protocol.hpp>

#include "threads/Timer.h"
#include "windowing/XBMC_events.h"
#include "windowing/XkbcommonKeymap.h"

namespace KODI
{
namespace WINDOWING
{
namespace WAYLAND
{

enum class InputType
{
  POINTER,
  KEYBOARD,
  TOUCH
};

/**
 * Handler interface for input events from \ref CSeatInputProcessor
 */
class IInputHandler
{
public:
  virtual ~IInputHandler() {}
  /**
   * Handle input event
   * \param seatGlobalName numeric Wayland global name of the seat the event occured on
   * \param type input device type that caused the event
   * \param event XBMC event data
   */
  virtual void OnEvent(std::uint32_t seatGlobalName, InputType type, XBMC_Event& event) {}
  /**
   * Handle focus enter
   * \param seatGlobalName numeric Wayland global name of the seat the event occured on
   * \param type input device type for which the surface has gained the focus
   */
  virtual void OnEnter(std::uint32_t seatGlobalName, InputType type) {}
  /**
   * Handle focus leave
   * \param seatGlobalName numeric Wayland global name of the seat the event occured on
   * \param type input device type for which the surface has lost the focus
   */
  virtual void OnLeave(std::uint32_t seatGlobalName, InputType type) {}
  /**
   * Handle request for setting the cursor
   * 
   * When the client gains pointer focus for a surface, a cursor image must be
   * attached to the pointer. Otherwise the previous pointer image would
   * be used.
   * 
   * This request is sent in addition to \ref OnEnter for \ref InputType::POINTER.
   * 
   * \param pointer pointer instance that needs its cursor set
   * \param serial Wayland protocol message serial that must be sent back in set_cursor
   */
  virtual void OnSetCursor(wayland::pointer_t& pointer, std::uint32_t serial) {}
};

/**
 * Handle all wl_seat-related events and process them into Kodi events
 */
class CSeatInputProcessor
{
public:
  /**
   * Construct seat input processor
   * \param globalName Wayland numeric global name of the seat
   * \param seat bound seat_t instance
   * \param handler handler that receives events from this seat, must not be null
   */
  CSeatInputProcessor(std::uint32_t globalName, wayland::seat_t const & seat, IInputHandler* handler);
  std::uint32_t GetGlobalName() const
  {
    return m_globalName;
  }
  std::string const& GetName() const
  {
    return m_name;
  }
  bool HasPointerCapability() const
  {
    return m_pointer;
  }
  bool HasKeyboardCapability() const
  {
    return m_keyboard;
  }
  bool HasTouchCapability() const
  {
    return m_touch;
  }
  void SetCoordinateScale(std::int32_t scale)
  {
    m_coordinateScale = scale;
  }

private:
  CSeatInputProcessor(CSeatInputProcessor const& other) = delete;
  CSeatInputProcessor& operator=(CSeatInputProcessor const& other) = delete;
  
  void HandleOnCapabilities(wayland::seat_capability caps);
  void HandlePointerCapability();
  void HandleKeyboardCapability();
  void HandleTouchCapability();

  std::uint16_t ConvertMouseCoordinate(double coord);
  void SetMousePosFromSurface(double x, double y);
  void SendMouseMotion();
  void SendMouseButton(unsigned char button, bool pressed);
  
  void ConvertAndSendKey(std::uint32_t scancode, bool pressed);
  XBMC_Event SendKey(unsigned char scancode, XBMCKey key, std::uint16_t unicodeCodepoint, bool pressed);
  
  std::uint32_t m_globalName;
  wayland::seat_t m_seat;
  std::string m_name = "<unknown>";

  IInputHandler* m_handler = nullptr;
  
  wayland::pointer_t m_pointer;
  wayland::keyboard_t m_keyboard;
  wayland::touch_t m_touch;
  
  std::int32_t m_coordinateScale = 1;
  // Pointer position in *scaled* coordinates
  std::uint16_t m_pointerX = 0;
  std::uint16_t m_pointerY = 0;
  
  std::unique_ptr<CXkbcommonContext> m_xkbContext;
  std::unique_ptr<CXkbcommonKeymap> m_keymap;
  // Default values are used if compositor does not send any
  std::atomic<int> m_keyRepeatDelay = {1000};
  std::atomic<int> m_keyRepeatInterval = {50};
  // Save complete XBMC_Event so no keymap lookups which might not be thread-safe
  // are needed in the repeat callback
  XBMC_Event m_keyToRepeat;
  
  class CKeyRepeatCallback : public ITimerCallback
  {
    CSeatInputProcessor* m_processor;
  public:
    CKeyRepeatCallback(CSeatInputProcessor* processor);
    void OnTimeout() override;
  };
  CKeyRepeatCallback m_keyRepeatCallback;
  CTimer m_keyRepeatTimer;
};

}
}
}
