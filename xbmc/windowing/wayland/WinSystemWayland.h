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

#include <time.h>

#include <atomic>
#include <ctime>
#include <list>
#include <map>
#include <set>

#include <wayland-client.hpp>
#include <wayland-cursor.hpp>

#include "Connection.h"
#include "Output.h"
#include "Seat.h"
#include "Signals.h"
#include "ShellSurface.h"
#include "threads/CriticalSection.h"
#include "WindowDecorationHandler.h"
#include "windowing/WinSystem.h"

class IDispResource;

namespace KODI
{
namespace WINDOWING
{
namespace WAYLAND
{

class CRegistry;
class CWindowDecorator;

class CWinSystemWayland : public CWinSystemBase, IInputHandler, IWindowDecorationHandler
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

  std::string GetClipboardText() override;

  void SetInhibitSkinReload(bool inhibit);

  float GetSyncOutputRefreshRate();
  float GetDisplayLatency() override;
  float GetFrameLatencyAdjustment() override;
  std::unique_ptr<CVideoSync> GetVideoSync(void* clock) override;
  
  void* GetVaDisplay();
  
  void Register(IDispResource* resource);
  void Unregister(IDispResource* resource);

  using PresentationFeedbackHandler = std::function<void(timespec /* tv */, std::uint32_t /* refresh */, std::uint32_t /* sync output id */, float /* sync output fps */, std::uint64_t /* msc */)>;
  CSignalRegistration RegisterOnPresentationFeedback(PresentationFeedbackHandler handler);
  
  // Like CWinSystemX11
  void GetConnectedOutputs(std::vector<std::string>* outputs);

protected:
  std::unique_ptr<KODI::WINDOWING::IOSScreenSaver> GetOSScreenSaverImpl() override;

  void PrepareFramePresentation();
  void FinishFramePresentation();

  std::unique_ptr<CConnection> m_connection;
  wayland::surface_t m_surface;

private:
  // IInputHandler
  void OnEnter(std::uint32_t seatGlobalName, InputType type) override;
  void OnLeave(std::uint32_t seatGlobalName, InputType type) override;
  void OnEvent(std::uint32_t seatGlobalName, InputType type, XBMC_Event& event) override;
  void OnSetCursor(wayland::pointer_t& pointer, std::uint32_t serial) override;

  // IWindowDecorationHandler
  void OnWindowMove(const wayland::seat_t& seat, std::uint32_t serial) override;
  void OnWindowResize(const wayland::seat_t& seat, std::uint32_t serial, wayland::shell_surface_resize edge) override;
  void OnWindowShowContextMenu(const wayland::seat_t& seat, std::uint32_t serial, CPointInt position) override;
  void OnWindowClose() override;
  void OnWindowMaximize() override;
  void OnWindowMinimize() override;

  // Registry handlers
  void OnSeatAdded(std::uint32_t name, wayland::proxy_t&& seat);
  void OnSeatRemoved(std::uint32_t name);
  void OnOutputAdded(std::uint32_t name, wayland::proxy_t&& output);
  void OnOutputRemoved(std::uint32_t name);

  void LoadDefaultCursor();
  void SendFocusChange(bool focus);
  void HandleSurfaceConfigure(std::uint32_t serial, CSizeInt size, IShellSurface::StateBitset state);
  bool ResetSurfaceSize(CSizeInt size, std::int32_t scale, bool fullScreen, bool fromConfigure);
  bool SetSize(CSizeInt configuredSize, IShellSurface::StateBitset state, bool sizeIncludesDecoration = true);
  
  std::string UserFriendlyOutputName(std::shared_ptr<COutput> const& output);
  std::shared_ptr<COutput> FindOutputByUserFriendlyName(std::string const& name);
  std::shared_ptr<COutput> FindOutputByWaylandOutput(wayland::output_t const& output);
  
  // Called when wl_output::done is received for an output, i.e. associated
  // information like modes is available
  void OnOutputDone(std::uint32_t name);
  void UpdateBufferScale();
  void ApplyBufferScale(std::int32_t scale);
  void UpdateTouchDpi();
  void ApplyShellSurfaceState();

  void AckConfigure(std::uint32_t serial);

  timespec GetPresentationClockTime();

  // Globals
  // -------
  std::unique_ptr<CRegistry> m_registry;
  /**
   * Registry used exclusively for wayland::seat_t
   * 
   * Use extra registry because seats can only be registered after the surface
   * has been created
   */
  std::unique_ptr<CRegistry> m_seatRegistry;
  wayland::compositor_t m_compositor;
  wayland::shm_t m_shm;
  wayland::presentation_t m_presentation;
  
  std::unique_ptr<IShellSurface> m_shellSurface;
  
  // Seat handling
  // -------------
  std::map<std::uint32_t, CSeat> m_seatProcessors;
  CCriticalSection m_seatProcessorsMutex;
  std::map<std::uint32_t, std::shared_ptr<COutput>> m_outputs;
  /// outputs that did not receive their done event yet
  std::map<std::uint32_t, std::shared_ptr<COutput>> m_outputsInPreparation;
  CCriticalSection m_outputsMutex;

  // Windowed mode
  // -------------
  std::unique_ptr<CWindowDecorator> m_windowDecorator;

  // Cursor
  // ------
  bool m_osCursorVisible = true;
  wayland::cursor_theme_t m_cursorTheme;
  wayland::buffer_t m_cursorBuffer;
  wayland::cursor_image_t m_cursorImage;
  wayland::surface_t m_cursorSurface;

  // Presentation feedback
  // ---------------------
  clockid_t m_presentationClock;
  struct SurfaceSubmission
  {
    timespec submissionTime;
    float latency;
    wayland::presentation_feedback_t feedback;
    SurfaceSubmission(timespec const& submissionTime, wayland::presentation_feedback_t const& feedback);
  };
  std::list<SurfaceSubmission> m_surfaceSubmissions;
  CCriticalSection m_surfaceSubmissionsMutex;
  /// Protocol object ID of the sync output returned by wp_presentation
  std::uint32_t m_syncOutputID;
  /// Refresh rate of sync output returned by wp_presentation
  std::atomic<float> m_syncOutputRefreshRate{0.0f};
  static const int LATENCY_MOVING_AVERAGE_SIZE = 30;
  std::atomic<float> m_latencyMovingAverage;
  CSignalHandlerList<PresentationFeedbackHandler> m_presentationFeedbackHandlers;
  std::uint64_t m_frameStartTime{};

  // IDispResource
  // -------------
  std::set<IDispResource*> m_dispResources;
  CCriticalSection m_dispResourcesMutex;

  // Surface state
  // -------------
  std::string m_currentOutput;
  /// Set of outputs that show some part of our main surface as indicated by
  /// compositor
  std::set<std::shared_ptr<COutput>> m_surfaceOutputs;
  /// Size of our surface in "surface coordinates", i.e. without scaling applied
  CSizeInt m_surfaceSize;
  /// Size of the whole window including window decorations as given by configure
  CSizeInt m_configuredSize;
  /// Scale in use for main surface buffer
  std::int32_t m_scale = 1;
  /// Shell surface state last acked
  IShellSurface::StateBitset m_shellSurfaceState;

  // Configure state
  // ---------------
  std::uint32_t m_currentConfigureSerial = 0;
  bool m_firstSerialAcked = false;
  std::uint32_t m_lastAckedSerial = 0;
  /// Shell surface state to be applied at next ack
  IShellSurface::StateBitset m_nextShellSurfaceState;
  /// Whether this is the first call to SetFullScreen
  bool m_isInitialSetFullScreen = true;
  bool m_inhibitSkinReload = false;
};


}
}
}
