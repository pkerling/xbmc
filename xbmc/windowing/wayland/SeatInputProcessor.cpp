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

#include "SeatInputProcessor.h"

#include "utils/log.h"

using namespace KODI::WINDOWING::WAYLAND;
using namespace std::placeholders;

namespace
{

/**
 * Handle change of availability of a wl_seat input capability
 * 
 * This checks whether the capability is currently available with the wl_seat
 * and whether it was bound to a processor. If there is a mismatch between
 * these two, the processor is destroyed if a capability was removed or created
 * if a capability was added.
 * 
 * \param handler CSeatInputProcessor instance
 * \param caps new capabilities
 * \param cap capability to check for
 * \param capName human-readable name of the capability for log messages
 * \param processor reference to a smart pointer that holds the
 *                  processor corresponding to the capability
 * \param instanceProvider function that functions as factory for the Wayland
 *                         protocol instance if the capability has been added
 * \param onNewCapability function that is called after setting the new capability
 *                        instance when it was added
 */
template<typename T, typename ProcessorPtrT, typename InstanceProviderT, typename OnNewCapabilityT>
void HandleCapabilityChange(CSeatInputProcessor* handler,
                            wayland::seat_capability caps,
                            wayland::seat_capability cap,
                            std::string const & capName,
                            ProcessorPtrT& processor,
                            InstanceProviderT instanceProvider,
                            OnNewCapabilityT onNewCapability)
{
  bool hasCapability = caps & cap;

  if ((!!processor) != hasCapability)
  {
    // Capability changed

    if (hasCapability)
    {
      // The capability was added
      CLog::Log(LOGDEBUG, "Wayland seat %s gained capability %s", handler->GetName().c_str(), capName.c_str());
      onNewCapability(instanceProvider());
    }
    else
    {
      // The capability was removed
      CLog::Log(LOGDEBUG, "Wayland seat %s lost capability %s", handler->GetName().c_str(), capName.c_str());
      processor.reset();
    }
  }
};

}

CSeatInputProcessor::CSeatInputProcessor(std::uint32_t globalName, const wayland::seat_t& seat, IInputHandler& handler)
: m_globalName(globalName), m_seat(seat), m_handler(handler)
{
  m_seat.on_name() = [this](std::string name)
  {
    m_name = name;
  };
  m_seat.on_capabilities() = std::bind(&CSeatInputProcessor::HandleOnCapabilities, this, std::placeholders::_1);
}

CSeatInputProcessor::~CSeatInputProcessor() = default;

void CSeatInputProcessor::HandleOnCapabilities(wayland::seat_capability caps)
{
  HandleCapabilityChange<wayland::pointer_t>
    (this,
     caps,
     wayland::seat_capability::pointer,
     "pointer",
     m_pointer,
     std::bind(&wayland::seat_t::get_pointer, &m_seat),
     std::bind(&CSeatInputProcessor::HandlePointerCapability, this, _1));
  HandleCapabilityChange<wayland::keyboard_t>
    (this,
     caps,
     wayland::seat_capability::keyboard,
     "keyboard",
     m_keyboard,
     std::bind(&wayland::seat_t::get_keyboard, &m_seat),
     std::bind(&CSeatInputProcessor::HandleKeyboardCapability, this, _1));
  HandleCapabilityChange<wayland::touch_t>
    (this,
     caps,
     wayland::seat_capability::touch,
     "touch",
     m_touch,
     std::bind(&wayland::seat_t::get_touch, &m_seat),
     std::bind(&CSeatInputProcessor::HandleTouchCapability, this, _1));
}

void CSeatInputProcessor::HandlePointerCapability(wayland::pointer_t const& pointer)
{
  m_pointer.reset(new CInputProcessorPointer(pointer, static_cast<IInputHandlerPointer&> (*this)));
  UpdateCoordinateScale();
}

void CSeatInputProcessor::OnPointerEnter(wayland::pointer_t& pointer, std::uint32_t serial)
{
  m_handler.OnSetCursor(pointer, serial);
  m_handler.OnEnter(m_globalName, InputType::POINTER);
}

void CSeatInputProcessor::OnPointerLeave()
{
  m_handler.OnLeave(m_globalName, InputType::POINTER);
}

void CSeatInputProcessor::OnPointerEvent(XBMC_Event& event)
{
  m_handler.OnEvent(m_globalName, InputType::POINTER, event);
}

void CSeatInputProcessor::HandleKeyboardCapability(wayland::keyboard_t const& keyboard)
{
  m_keyboard.reset(new CInputProcessorKeyboard(keyboard, static_cast<IInputHandlerKeyboard&> (*this)));
}

void CSeatInputProcessor::OnKeyboardEnter()
{
  m_handler.OnEnter(m_globalName, InputType::KEYBOARD);
}

void CSeatInputProcessor::OnKeyboardLeave()
{
  m_handler.OnLeave(m_globalName, InputType::KEYBOARD);
}

void CSeatInputProcessor::OnKeyboardEvent(XBMC_Event& event)
{
  m_handler.OnEvent(m_globalName, InputType::KEYBOARD, event);
}

void CSeatInputProcessor::HandleTouchCapability(wayland::touch_t const& touch)
{
  m_touch.reset(new CInputProcessorTouch(touch));
  UpdateCoordinateScale();
}

void CSeatInputProcessor::SetCoordinateScale(std::int32_t scale)
{
  m_coordinateScale = scale;
  UpdateCoordinateScale();
}

void CSeatInputProcessor::UpdateCoordinateScale()
{
  if (m_pointer)
  {
    m_pointer->SetCoordinateScale(m_coordinateScale);
  }
  if (m_touch)
  {
    m_touch->SetCoordinateScale(m_coordinateScale);
  }
}