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

#include <map>
#include <set>

#include <wayland-client.hpp>
#include <wayland-cursor.hpp>

#include "Connection.h"
#include "Output.h"
#include "SeatInputProcessor.h"
#include "ShellSurface.h"
#include "threads/CriticalSection.h"
#include "windowing/WinSystem.h"

class IDispResource;

namespace KODI
{
namespace WINDOWING
{
namespace WAYLAND
{

class CWinSystemWayland : public CWinSystemBase, public IInputHandler, public IConnectionHandler
{
public:
  CWinSystemWayland();
  virtual ~CWinSystemWayland();

  bool InitWindowSystem() override;
  bool DestroyWindowSystem() override;

  bool CreateNewWindow(const std::string& name,
                       bool fullScreen,
                       RESOLUTION_INFO& res) override;

  bool DestroyWindow() override;

  bool ResizeWindow(int newWidth, int newHeight, int newLeft, int newTop) override;
  bool SetFullScreen(bool fullScreen, RESOLUTION_INFO& res, bool blankOtherDisplays) override;

  void UpdateResolutions() override;
  int GetNumScreens() override;
  int GetCurrentScreen() override;

  bool CanDoWindowed() override;
  bool Hide() override;
  bool Show(bool raise = true) override;
  
  bool HasCursor() override;
  void ShowOSMouse(bool show) override;

  void SetInhibitSkinReload(bool inhibit);
  
  void* GetVaDisplay();
  
  virtual void Register(IDispResource *resource);
  virtual void Unregister(IDispResource *resource);
  
  // IInputHandler
  void OnEnter(std::uint32_t seatGlobalName, InputType type) override;
  void OnLeave(std::uint32_t seatGlobalName, InputType type) override;
  void OnEvent(std::uint32_t seatGlobalName, InputType type, XBMC_Event& event) override;
  void OnSetCursor(wayland::pointer_t& pointer, std::uint32_t serial) override;

  // IConnectionHandler
  void OnSeatAdded(std::uint32_t name, wayland::seat_t& seat) override;
  void OnOutputAdded(std::uint32_t name, wayland::output_t& output) override;
  void OnGlobalRemoved(std::uint32_t name) override;
  
  // Like CWinSystemX11
  void GetConnectedOutputs(std::vector<std::string> *outputs);

protected:
  std::unique_ptr<KODI::WINDOWING::IOSScreenSaver> GetOSScreenSaverImpl() override;

  void LoadDefaultCursor();
  void SendFocusChange(bool focus);
  void HandleSurfaceConfigure(std::uint32_t serial, std::int32_t width, std::int32_t height);
  bool ResetSurfaceSize(std::int32_t width, std::int32_t height, std::int32_t scale);
  bool SetSizeFromSurfaceSize(std::int32_t surfaceWidth, std::int32_t surfaceHeight);
  
  std::string UserFriendlyOutputName(std::shared_ptr<COutput> const& output);
  std::shared_ptr<COutput> FindOutputByUserFriendlyName(std::string const& name);
  std::shared_ptr<COutput> FindOutputByWaylandOutput(wayland::output_t const& output);
  
  // Called when wl_output::done is received for an output, i.e. associated
  // information like modes is available
  void OnOutputDone(std::uint32_t name);
  void UpdateBufferScale();
  void ApplyBufferScale(std::int32_t scale);

  void AckConfigure(std::uint32_t serial);
  
  std::unique_ptr<CConnection> m_connection;
  wayland::surface_t m_surface;
  std::unique_ptr<IShellSurface> m_shellSurface;
  
  std::map<std::uint32_t, CSeatInputProcessor> m_seatProcessors;
  CCriticalSection m_seatProcessorsMutex;
  // m_outputsInPreparation did not receive their done event yet
  std::map<std::uint32_t, std::shared_ptr<COutput>> m_outputs, m_outputsInPreparation;
  CCriticalSection m_outputsMutex;
  
  bool m_osCursorVisible = true;
  wayland::cursor_theme_t m_cursorTheme;
  wayland::buffer_t m_cursorBuffer;
  wayland::cursor_image_t m_cursorImage;
  wayland::surface_t m_cursorSurface;
  
  std::set<IDispResource*> m_dispResources;
  CCriticalSection m_dispResourcesMutex;

  bool m_inhibitSkinReload = false;

  std::string m_currentOutput;
  // Set of outputs that show some part of our main surface as indicated by
  // compositor
  std::set<std::shared_ptr<COutput>> m_surfaceOutputs;
  // Size of our surface in "surface coordinates", i.e. without scaling applied
  std::int32_t m_surfaceWidth, m_surfaceHeight;
  std::int32_t m_scale = 1;
  std::uint32_t m_currentConfigureSerial = 0;
  bool m_firstSerialAcked = false;
  std::uint32_t m_lastAckedSerial = 0;
  // Whether this is the first call to SetFullScreen
  bool m_isInitialSetFullScreen = true;
};


}
}
}
