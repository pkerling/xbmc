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

#include "WinSystemWayland.h"

#include <algorithm>
#include <limits>

#if defined(HAVE_LIBVA)
#include <va/va_wayland.h>
#endif

#include "Application.h"
#include "Connection.h"
#include "guilib/DispResource.h"
#include "guilib/GraphicContext.h"
#include "guilib/LocalizeStrings.h"
#include "input/InputManager.h"
#include "ServiceBroker.h"
#include "settings/DisplaySettings.h"
#include "settings/Settings.h"
#include "ShellSurfaceWlShell.h"
#include "ShellSurfaceXdgShellUnstableV6.h"
#include "threads/SingleLock.h"
#include "utils/log.h"
#include "utils/StringUtils.h"
#include "WinEventsWayland.h"

using namespace KODI::WINDOWING::WAYLAND;
using namespace std::placeholders;

CWinSystemWayland::CWinSystemWayland() :
CWinSystemBase()
{
  m_eWindowSystem = WINDOW_SYSTEM_WAYLAND;
}

CWinSystemWayland::~CWinSystemWayland()
{
  DestroyWindowSystem();
}

bool CWinSystemWayland::InitWindowSystem()
{
  wayland::set_log_handler([](std::string message)
  {
    CLog::Log(LOGWARNING, "wayland-client log message: %s", message.c_str()); });

  CLog::LogFunction(LOGINFO, "CWinSystemWayland::InitWindowSystem", "Connecting to Wayland server");
  m_connection.reset(new CConnection(this));
  if (m_seatProcessors.empty())
  {
    CLog::Log(LOGWARNING, "Wayland compositor did not announce a wl_seat - you will not have any input devices for the time being");
  }
  // Do another roundtrip to get initial wl_output information
  int tries = 0;
  while (m_outputs.empty())
  {
    if (tries++ > 5)
    {
      throw std::runtime_error("No outputs received from compositor");
    }
    if (m_connection->GetDisplay().roundtrip() < 0)
    {
      throw std::runtime_error("Wayland roundtrip failed");
    }
  }
  
  CWinEventsWayland::SetDisplay(&m_connection->GetDisplay());

  // pointer is by default not on this window, will be immediately rectified
  // by the enter() events if it is
  CInputManager::GetInstance().SetMouseActive(false);

  return CWinSystemBase::InitWindowSystem();
}

bool CWinSystemWayland::DestroyWindowSystem()
{
  // Make sure no more events get processed when we kill the instances
  CWinEventsWayland::SetDisplay(nullptr);

  DestroyWindow();
  // wl_display_disconnect frees all proxy objects, so we have to make sure
  // all stuff is gone on the C++ side before that
  m_cursorSurface = wayland::surface_t();
  m_cursorBuffer = wayland::buffer_t();
  m_cursorImage = wayland::cursor_image_t();
  m_cursorTheme = wayland::cursor_theme_t();
  m_seatProcessors.clear();
  m_outputs.clear();

  m_connection.reset();
  return CWinSystemBase::DestroyWindowSystem();
}

bool CWinSystemWayland::CreateNewWindow(const std::string& name,
                                        bool fullScreen,
                                        RESOLUTION_INFO& res)
{
  m_surface = m_connection->GetCompositor().create_surface();
  m_shellSurface.reset(new CShellSurfaceWlShell(m_connection->GetShell(), m_surface, name, "kodi"));
  auto xdgShell = m_connection->GetXdgShellUnstableV6();
  if (xdgShell)
  {
    m_shellSurface.reset(new CShellSurfaceXdgShellUnstableV6(m_connection->GetDisplay(), xdgShell, m_surface, name, "kodi"));
  }
  else
  {
    CLog::Log(LOGWARNING, "Compositor does not support xdg_shell unstable v6 protocol - falling back to wl_shell, not all features might work");
    m_shellSurface.reset(new CShellSurfaceWlShell(m_connection->GetShell(), m_surface, name, "kodi"));  
  }
  
  m_shellSurface->Initialize();
  
  m_shellSurface->OnConfigure() = std::bind(&CWinSystemWayland::HandleSurfaceConfigure, this, _1, _2);

  return true;
}

bool CWinSystemWayland::DestroyWindow()
{
  m_shellSurface.reset();
  // waylandpp automatically calls wl_surface_destroy when the last reference is removed
  m_surface = wayland::surface_t();

  return true;
}

bool CWinSystemWayland::CanDoWindowed()
{
  return false;
}

int CWinSystemWayland::GetNumScreens()
{
  // Multiple screen/resolution support in core Kodi badly needs refactoring, but as
  // it touches a lot of code we just do it like X11 for the moment:
  // Pretend that there is only one screen, show more screens with
  // custom names in the GUI using an #ifdef in DisplaySettings
  // - otherwise we would just get a selection between "Full Screen #1" and
  // "Full Screen #2" etc. instead of actual monitor names.
  return 1;
}

int CWinSystemWayland::GetCurrentScreen()
{
  // See GetNumScreens()
  return 1;
}

void CWinSystemWayland::GetConnectedOutputs(std::vector<std::string>* outputs)
{
  CSingleLock lock(m_outputsMutex);
  std::transform(m_outputs.cbegin(), m_outputs.cend(), std::back_inserter(*outputs),
                 [this](decltype(m_outputs)::value_type const& pair)
                 {
                   return UserFriendlyOutputName(pair.second); });
}

void CWinSystemWayland::UpdateResolutions()
{
  CWinSystemBase::UpdateResolutions();

  CDisplaySettings::GetInstance().ClearCustomResolutions();

  // Mimic X11:
  // Only show resolutions for the currently selected output
  std::string userOutput = CServiceBroker::GetSettings().GetString(CSettings::SETTING_VIDEOSCREEN_MONITOR);

  CSingleLock lock(m_outputsMutex);
  
  if (m_outputs.empty())
  {
    // *Usually* this should not happen - just give up
    return;
  }
  
  COutput* output = FindOutputByUserFriendlyName(userOutput);
  if (!output)
  {
    // Fallback to current output
    output = FindOutputByUserFriendlyName(m_currentOutput);
  }
  if (!output)
  {
    // Well just use the first one
    output = &m_outputs.begin()->second;
  }
  
  std::string outputName = UserFriendlyOutputName(*output);

  auto const& modes = output->GetModes();
  // TODO wait until output has all information
  auto const& currentMode = output->GetCurrentMode();
  auto physicalSize = output->GetPhysicalSize();
  CLog::Log(LOGINFO, "User wanted output \"%s\", we now have \"%s\" size %dx%d mm with %zu mode(s):", userOutput.c_str(), outputName.c_str(), std::get<0>(physicalSize), std::get<1>(physicalSize), modes.size());

  for (auto const& mode : modes)
  {
    bool isCurrent = (mode == currentMode);
    float pixelRatio = output->GetPixelRatioForMode(mode);
    CLog::Log(LOGINFO, "- %dx%d @%.3f Hz pixel ratio %.3f%s", mode.width, mode.height, mode.refreshMilliHz / 1000.0f, pixelRatio, isCurrent ? " current" : "");

    RESOLUTION_INFO res(mode.width, mode.height);
    res.bFullScreen = true;
    res.iScreen = 0; // not used
    res.strOutput = outputName;
    res.fPixelRatio = pixelRatio;
    res.fRefreshRate = mode.refreshMilliHz / 1000.0f;
    g_graphicsContext.ResetOverscan(res);

    if (isCurrent)
    {
      CDisplaySettings::GetInstance().GetResolutionInfo(RES_DESKTOP) = res;
    }
    else
    {
      CDisplaySettings::GetInstance().AddResolutionInfo(res);
    }
  }

  CDisplaySettings::GetInstance().ApplyCalibrations();
}

bool CWinSystemWayland::ResizeWindow(int newWidth, int newHeight, int newLeft, int newTop)
{
  // Windowed mode is unsupported
  return false;
}

COutput* CWinSystemWayland::FindOutputByUserFriendlyName(const std::string& name)
{
  CSingleLock lock(m_outputsMutex);
  auto outputIt = std::find_if(m_outputs.begin(), m_outputs.end(),
                               [this, &name](decltype(m_outputs)::value_type const& entry)
                               {
                                 return (name == UserFriendlyOutputName(entry.second));
                               });

  return (outputIt == m_outputs.end() ? nullptr : &outputIt->second);
}

bool CWinSystemWayland::SetFullScreen(bool fullScreen, RESOLUTION_INFO& res, bool blankOtherDisplays)
{
  if (m_currentOutput == res.strOutput && m_nWidth == res.iWidth && m_nHeight == res.iHeight && m_fRefreshRate == res.fRefreshRate && m_bFullScreen == fullScreen)
  {
    // Nothing to do
    return true;
  }

  CLog::Log(LOGINFO, "Wayland trying to switch mode to %dx%d @%.3f Hz on output \"%s\"", res.iWidth, res.iHeight, res.fRefreshRate, res.strOutput.c_str());

  // Try to match output
  wayland::output_t output;
  {
    CSingleLock lock(m_outputsMutex);
    
    COutput* outputHandler = FindOutputByUserFriendlyName(res.strOutput);
    if (outputHandler)
    {
      output = outputHandler->GetWaylandOutput();
      CLog::Log(LOGDEBUG, "Resolved output \"%s\" to bound Wayland global %u", res.strOutput.c_str(), outputHandler->GetGlobalName());
    }
    else
    {
      CLog::Log(LOGINFO, "Could not match output \"%s\" to a currently available Wayland output, falling back to default output", res.strOutput.c_str());
    }
    // Release lock only when output has been assigned to local variable so it
    // cannot go away
  }

  m_nWidth = res.iWidth;
  m_nHeight = res.iHeight;
  m_bFullScreen = fullScreen;
  // This is just a guess since the compositor is free to ignore our frame rate
  // request
  m_fRefreshRate = res.fRefreshRate;
  // There is -no- guarantee that the compositor will put the surface on this
  // screen, but pretend that it does so we have any information at all
  m_currentOutput = res.strOutput;

  if (fullScreen)
  {
    m_shellSurface->SetFullScreen(output, m_fRefreshRate);
  }
  else
  {
    // Shouldn't happen since we claim not to support windowed modes
    CLog::Log(LOGWARNING, "Wayland windowing system asked to switch to windowed mode which is not really supported");
    m_shellSurface->SetWindowed();
  }

  return true;
}

void CWinSystemWayland::HandleSurfaceConfigure(std::int32_t width, std::int32_t height)
{
  CLog::Log(LOGINFO, "Got Wayland surface size %dx%d", width, height);

  // Mark everything opaque so the compositor can render it faster
  wayland::region_t opaqueRegion = m_connection->GetCompositor().create_region();
  opaqueRegion.add(0, 0, width, height);
  m_surface.set_opaque_region(opaqueRegion);
  // No surface commit, EGL context will do that when it changes the buffer

  if (m_nWidth == width && m_nHeight == height)
  {
    // Nothing to do
    return;
  }

  m_nWidth = width;
  m_nHeight = height;
  // Update desktop resolution
  auto& res = CDisplaySettings::GetInstance().GetCurrentResolutionInfo();
  res.iWidth = width;
  res.iHeight = height;
  res.iScreenWidth = width;
  res.iScreenHeight = height;
  res.iSubtitles = (int) (0.965 * height);
  g_graphicsContext.ResetOverscan(res);
  CDisplaySettings::GetInstance().ApplyCalibrations();
  // Force resolution update
  g_graphicsContext.SetVideoResolution(g_graphicsContext.GetVideoResolution(), true);
}

std::string CWinSystemWayland::UserFriendlyOutputName(const COutput& output)
{
  std::vector<std::string> parts;
  if (!output.GetMake().empty())
  {
    parts.emplace_back(output.GetMake());
  }
  if (!output.GetModel().empty())
  {
    parts.emplace_back(output.GetModel());
  }
  if (parts.empty())
  {
    // Fallback to "unknown" if no name received from compositor
    parts.emplace_back(g_localizeStrings.Get(13205));
  }

  // Add position
  std::int32_t x, y;
  std::tie(x, y) = output.GetPosition();
  if (x != 0 || y != 0)
  {
    parts.emplace_back(StringUtils::Format("@{}x{}", x, y));
  }

  return StringUtils::Join(parts, " ");
}

bool CWinSystemWayland::Hide()
{
  // wl_shell_surface does not really support this - wait for xdg_shell
  return false;
}

bool CWinSystemWayland::Show(bool raise)
{
  // wl_shell_surface does not really support this - wait for xdg_shell
  return true;
}

bool CWinSystemWayland::HasCursor()
{
  CSingleLock lock(m_seatProcessorsMutex);
  return std::any_of(m_seatProcessors.cbegin(), m_seatProcessors.cend(),
                     [](decltype(m_seatProcessors)::value_type const& entry)
                     {
                       return entry.second.HasPointerCapability();
                     });
}

void CWinSystemWayland::ShowOSMouse(bool show)
{
  m_osCursorVisible = show;
}

void CWinSystemWayland::LoadDefaultCursor()
{
  if (!m_cursorSurface)
  {
    // Load default cursor theme and default cursor
    // Size of 16px is somewhat random
    // Cursor theme must be kept around since the lifetime of the image buffers
    // is coupled to it
    m_cursorTheme = wayland::cursor_theme_t("", 16, m_connection->GetShm());
    wayland::cursor_t cursor;
    try
    {
      cursor = m_cursorTheme.get_cursor("default");
    }
    catch (std::exception& e)
    {
      CLog::Log(LOGWARNING, "Could not load default cursor from theme, continuing without OS cursor");
    }
    // Just use the first image, do not handle animation
    m_cursorImage = cursor.image(0);
    m_cursorBuffer = m_cursorImage.get_buffer();
    m_cursorSurface = m_connection->GetCompositor().create_surface();
  }
  // Attach buffer to a surface - it seems that the compositor may change
  // the cursor surface when the pointer leaves our surface, so we reattach the
  // buffer each time
  m_cursorSurface.attach(m_cursorBuffer, 0, 0);
  m_cursorSurface.damage(0, 0, m_cursorImage.width(), m_cursorImage.height());
  m_cursorSurface.commit();
}

void CWinSystemWayland::Register(IDispResource* resource)
{
  CSingleLock lock(m_dispResourcesMutex);
  m_dispResources.emplace(resource);
}

void CWinSystemWayland::Unregister(IDispResource* resource)
{
  CSingleLock lock(m_dispResourcesMutex);
  m_dispResources.erase(resource);
}

void CWinSystemWayland::OnSeatAdded(std::uint32_t name, wayland::seat_t& seat)
{
  CSingleLock lock(m_seatProcessorsMutex);
  m_seatProcessors.emplace(std::piecewise_construct, std::forward_as_tuple(name), std::forward_as_tuple(name, seat, this));
}

void CWinSystemWayland::OnOutputAdded(std::uint32_t name, wayland::output_t& output)
{
  // This is not accessed from multiple threads
  m_outputsInPreparation.emplace(std::piecewise_construct,
                                 std::forward_as_tuple(name),
                                 std::forward_as_tuple(name, output, std::bind(&CWinSystemWayland::OnOutputDone, this, name)));
}

void CWinSystemWayland::OnOutputDone(std::uint32_t name)
{
  auto it = m_outputsInPreparation.find(name);
  if (it == m_outputsInPreparation.end())
  {
    return;
  }
  
  CSingleLock lock(m_outputsMutex);
  // Move from m_outputsInPreparation to m_outputs
  m_outputs.emplace(std::move(*it));
  m_outputsInPreparation.erase(it);
}

void CWinSystemWayland::OnGlobalRemoved(std::uint32_t name)
{
  {
    CSingleLock lock(m_seatProcessorsMutex);
    m_seatProcessors.erase(name);
  }
  {
    m_outputsInPreparation.erase(name);
    CSingleLock lock(m_outputsMutex);
    if (m_outputs.erase(name) != 0)
    {
      // TODO Handle: Update resolution etc.
    }
  }
}

void CWinSystemWayland::SendFocusChange(bool focus)
{
  g_application.m_AppFocused = focus;
  CSingleLock lock(m_dispResourcesMutex);
  for (auto dispResource : m_dispResources)
  {
    dispResource->OnAppFocusChange(focus);
  }
}

void CWinSystemWayland::OnEnter(std::uint32_t seatGlobalName, InputType type)
{
  // Couple to keyboard focus
  if (type == InputType::KEYBOARD)
  {
    SendFocusChange(true);
  }
  if (type == InputType::POINTER)
  {
    CInputManager::GetInstance().SetMouseActive(true);
  }
}

void CWinSystemWayland::OnLeave(std::uint32_t seatGlobalName, InputType type)
{
  // Couple to keyboard focus
  if (type == InputType::KEYBOARD)
  {
    SendFocusChange(false);
  }
  if (type == InputType::POINTER)
  {
    CInputManager::GetInstance().SetMouseActive(false);
  }
}

void CWinSystemWayland::OnEvent(std::uint32_t seatGlobalName, InputType type, XBMC_Event& event)
{
  CWinEvents::MessagePush(&event);
}

void CWinSystemWayland::OnSetCursor(wayland::pointer_t& pointer, std::uint32_t serial)
{
  if (m_osCursorVisible)
  {
    LoadDefaultCursor();
    if (m_cursorSurface) // Cursor loading could have failed
    {
      pointer.set_cursor(serial, m_cursorSurface, m_cursorImage.hotspot_x(), m_cursorImage.hotspot_y());
    }
  }
  else
  {
    pointer.set_cursor(serial, wayland::surface_t(), 0, 0);
  }
}

#if defined(HAVE_LIBVA)
void* CWinSystemWayland::GetVaDisplay()
{
  return vaGetDisplayWl(reinterpret_cast<wl_display*> (m_connection->GetDisplay().c_ptr()));
}
#endif
