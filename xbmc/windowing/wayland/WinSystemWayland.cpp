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
#include "utils/MathUtils.h"

using namespace KODI::WINDOWING::WAYLAND;
using namespace std::placeholders;

namespace
{

// Caller should hold g_graphicsContext lock
RESOLUTION FindMatchingCustomResolution(int width, int height, float refreshRate)
{
  for (size_t res = RES_DESKTOP; res < CDisplaySettings::GetInstance().ResolutionInfoSize(); ++res)
  {
    auto const& resInfo = CDisplaySettings::GetInstance().GetResolutionInfo(res);
    if (resInfo.iWidth == width && resInfo.iHeight == height && MathUtils::FloatEquals(resInfo.fRefreshRate, refreshRate, 0.0005f))
    {
      return static_cast<RESOLUTION> (res);
    }
  }
  return RES_INVALID;
}

struct OutputScaleComparer
{
  bool operator()(std::shared_ptr<COutput> const& output1, std::shared_ptr<COutput> const& output2)
  {
    return output1->GetScale() < output2->GetScale();
  }
};

struct OutputCurrentRefreshRateComparer
{
  bool operator()(std::shared_ptr<COutput> const& output1, std::shared_ptr<COutput> const& output2)
  {
    return output1->GetCurrentMode().refreshMilliHz < output2->GetCurrentMode().refreshMilliHz;
  }
};

const std::string CONFIGURE_RES_ID = "configure";

}

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
  if (m_connection->GetDisplay().roundtrip() < 0)
  {
    throw std::runtime_error("Wayland roundtrip failed");
  }
  if (m_outputs.empty())
  {
    throw std::runtime_error("No outputs received from compositor");
  }

  // Event loop is started in CreateWindow

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
  m_outputsInPreparation.clear();
  m_outputs.clear();
  m_surfaceOutputs.clear();

  m_connection.reset();
  return CWinSystemBase::DestroyWindowSystem();
}

bool CWinSystemWayland::CreateNewWindow(const std::string& name,
                                        bool fullScreen,
                                        RESOLUTION_INFO& res)
{
  m_surface = m_connection->GetCompositor().create_surface();
  m_surface.on_enter() = [this](wayland::output_t wloutput)
  {
    if (auto output = FindOutputByWaylandOutput(wloutput))
    {
      CLog::Log(LOGDEBUG, "Entering output \"%s\" with scale %d", UserFriendlyOutputName(output).c_str(), output->GetScale());
      m_surfaceOutputs.emplace(output);
      UpdateBufferScale();
    }
    else
    {
      CLog::Log(LOGWARNING, "Entering output that was not configured yet, ignoring");
    }
  };
  m_surface.on_leave() = [this](wayland::output_t wloutput)
  {    
    if (auto output = FindOutputByWaylandOutput(wloutput))
    {
      CLog::Log(LOGDEBUG, "Leaving output \"%s\" with scale %d", UserFriendlyOutputName(output).c_str(), output->GetScale());
      m_surfaceOutputs.erase(output);
      UpdateBufferScale();
    }
    else
    {
      CLog::Log(LOGWARNING, "Leaving output that was not configured yet, ignoring");
    }
  };

  // Try with this resolution if compositor does not say otherwise
  SetSizeFromSurfaceSize(res.iWidth, res.iHeight);

  auto xdgShell = m_connection->GetXdgShellUnstableV6();
  if (xdgShell)
  {
    m_shellSurface.reset(new CShellSurfaceXdgShellUnstableV6(m_connection->GetDisplay(), xdgShell, m_surface, name, "kodi"));
  }
  else
  {
    CLog::LogF(LOGWARNING, "Compositor does not support xdg_shell unstable v6 protocol - falling back to wl_shell, not all features might work");
    m_shellSurface.reset(new CShellSurfaceWlShell(m_connection->GetShell(), m_surface, name, "kodi"));
  }

  // Just remember initial width/height for context creation
  // This is used for sizing the EGLSurface
  m_shellSurface->OnConfigure() = [this](std::uint32_t serial, std::int32_t width, std::int32_t height)
  {
    CLog::Log(LOGINFO, "Got initial Wayland surface size %dx%d", width, height);
    SetSizeFromSurfaceSize(width, height);
    AckConfigure(serial);
  };

  if (fullScreen)
  {
    // Try to start on correct monitor and with correct buffer scale
    auto output = FindOutputByUserFriendlyName(CServiceBroker::GetSettings().GetString(CSettings::SETTING_VIDEOSCREEN_MONITOR));
    if (output)
    {
      m_shellSurface->SetFullScreen(output->GetWaylandOutput(), res.fRefreshRate);
      m_scale = output->GetScale();
      ApplyBufferScale(m_scale);
    }
  }

  m_shellSurface->Initialize();

  // Update resolution with real size as it could have changed due to configure()
  UpdateDesktopResolution(res, 0, m_nWidth, m_nHeight, res.fRefreshRate);

  // Set real handler during runtime
  m_shellSurface->OnConfigure() = std::bind(&CWinSystemWayland::HandleSurfaceConfigure, this, _1, _2, _3);

  // Now start processing events
  //
  // There are two stages to the event handling:
  // * Initialization (which ends here): Everything runs synchronously and init
  //   code that needs events processed must call roundtrip().
  //   This is done for simplicity because it is a lot easier than to make
  //   everything thread-safe everywhere in the startup code, which is also
  //   not really necessary.
  // * Runtime (which starts here): Every object creation from now on
  //   needs to take great care to be thread-safe:
  //   Since the event pump is always running now, there is a tiny window between
  //   creating an object and attaching the C++ event handlers during which
  //   events can get queued and dispatched for the object but the handlers have
  //   not been set yet. Consequently, the events would get lost.
  //   However, this does not apply to objects that are created in response to
  //   compositor events. Since the callbacks are called from the event processing
  //   thread and ran strictly sequentially, no other events are dispatched during
  //   the runtime of a callback. Luckily this applies to global binding like
  //   wl_output and wl_seat and thus to most if not all runtime object creation
  //   cases we have to support.
  CWinEventsWayland::SetDisplay(&m_connection->GetDisplay());

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

  auto output = FindOutputByUserFriendlyName(userOutput);
  if (!output)
  {
    // Fallback to current output
    output = FindOutputByUserFriendlyName(m_currentOutput);
  }
  if (!output)
  {
    // Well just use the first one
    output = m_outputs.begin()->second;
  }

  std::string outputName = UserFriendlyOutputName(output);

  auto const& modes = output->GetModes();
  auto const& currentMode = output->GetCurrentMode();
  auto physicalSize = output->GetPhysicalSize();
  CLog::LogF(LOGINFO, "User wanted output \"%s\", we now have \"%s\" size %dx%d mm with %zu mode(s):", userOutput.c_str(), outputName.c_str(), std::get<0>(physicalSize), std::get<1>(physicalSize), modes.size());

  for (auto const& mode : modes)
  {
    bool isCurrent = (mode == currentMode);
    float pixelRatio = output->GetPixelRatioForMode(mode);
    CLog::LogF(LOGINFO, "- %dx%d @%.3f Hz pixel ratio %.3f%s", mode.width, mode.height, mode.refreshMilliHz / 1000.0f, pixelRatio, isCurrent ? " current" : "");

    RESOLUTION_INFO res;
    UpdateDesktopResolution(res, 0, mode.width, mode.height, mode.refreshMilliHz / 1000.0f);
    res.strOutput = outputName;
    res.fPixelRatio = pixelRatio;

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

std::shared_ptr<COutput> CWinSystemWayland::FindOutputByUserFriendlyName(const std::string& name)
{
  CSingleLock lock(m_outputsMutex);
  auto outputIt = std::find_if(m_outputs.begin(), m_outputs.end(),
                               [this, &name](decltype(m_outputs)::value_type const& entry)
                               {
                                 return (name == UserFriendlyOutputName(entry.second));
                               });

  return (outputIt == m_outputs.end() ? nullptr : outputIt->second);
}

std::shared_ptr<COutput> CWinSystemWayland::FindOutputByWaylandOutput(wayland::output_t const& output)
{
  CSingleLock lock(m_outputsMutex);
  auto outputIt = std::find_if(m_outputs.begin(), m_outputs.end(),
                               [this, &output](decltype(m_outputs)::value_type const& entry)
                               {
                                 return (output == entry.second->GetWaylandOutput());
                               });

  return (outputIt == m_outputs.end() ? nullptr : outputIt->second);
}

bool CWinSystemWayland::SetFullScreen(bool fullScreen, RESOLUTION_INFO& res, bool blankOtherDisplays)
{
  // FIXME Our configuration is protected by graphicsContext lock
  // If we'd use a mutex private to this class, we would have to lock both
  // that one and graphicsContext (because the resolutions get updated),
  // leading to a possible deadlock.
  CSingleLock lock(g_graphicsContext);

  CLog::LogF(LOGINFO, "Wayland asked to switch mode to %dx%d @%.3f Hz on output \"%s\"", res.iWidth, res.iHeight, res.fRefreshRate, res.strOutput.c_str());

  // In fullscreen modes, we never change the surface size on Kodi's request,
  // but only when the compositor tells us to. At least xdg_shell specifies
  // that with state fullscreen the dimensions given in configure() must
  // always be observed.
  // This does mean that the compositor has no way of knowing which resolution
  // we would (in theory) want. Since no compositor implements dynamic resolution
  // switching at the moment, this is not a problem. If it is some day implemented
  // in compositors, this code must be changed to match the behavior that is
  // expected then anyway.

  m_bFullScreen = fullScreen;

  bool wasConfigure = (res.strId == CONFIGURE_RES_ID);
  // Reset configure flag
  // Setting it in res will not modify the global information in CDisplaySettings
  // and we don't know which resolution index this is, so just reset all
  for (size_t resIdx = RES_DESKTOP; resIdx < CDisplaySettings::GetInstance().ResolutionInfoSize(); resIdx++)
  {
    CDisplaySettings::GetInstance().GetResolutionInfo(resIdx).strId = "";
  }

  if (fullScreen)
  {
    if (!wasConfigure || m_currentOutput != res.strOutput)
    {
      // There is -no- guarantee that the compositor will put the surface on this
      // screen, but pretend that it does so we have any information at all
      m_currentOutput = res.strOutput;

      // Try to match output
      auto output = FindOutputByUserFriendlyName(res.strOutput);
      if (output)
      {
        CLog::LogF(LOGDEBUG, "Resolved output \"%s\" to bound Wayland global %u", res.strOutput.c_str(), output->GetGlobalName());
      }
      else
      {
        CLog::LogF(LOGINFO, "Could not match output \"%s\" to a currently available Wayland output, falling back to default output", res.strOutput.c_str());
      }
      
      CLog::LogF(LOGDEBUG, "Setting full-screen with refresh rate %.3f", res.fRefreshRate);
      m_shellSurface->SetFullScreen(output ? output->GetWaylandOutput() : wayland::output_t(), res.fRefreshRate);
    }
    else
    {
      // Switch done, do not SetFullScreen() again - otherwise we would
      // get an endless repetition of setting full screen and configure events
      CLog::LogF(LOGDEBUG, "Called in response to surface configure, not calling set_fullscreen on surface");
    }
  }
  else
  {
    // Shouldn't happen since we claim not to support windowed modes
    CLog::LogF(LOGWARNING, "Wayland windowing system asked to switch to windowed mode which is not really supported");
    m_shellSurface->SetWindowed();
  }

  if (wasConfigure)
  {
    // Mark everything opaque so the compositor can render it faster
    // Do it here so size always matches the configured egl surface
    CLog::LogF(LOGDEBUG, "Setting opaque region size %dx%d", m_surfaceWidth, m_surfaceHeight);
    wayland::region_t opaqueRegion = m_connection->GetCompositor().create_region();
    opaqueRegion.add(0, 0, m_surfaceWidth, m_surfaceHeight);
    m_surface.set_opaque_region(opaqueRegion);
    // Buffer scale must also match egl size configuration
    ApplyBufferScale(m_scale);

    // Next buffer that the graphic context attaches will have the size corresponding
    // to this configure, so go and ack it
    AckConfigure(m_currentConfigureSerial);
  }

  bool wasInitialSetFullScreen = m_isInitialSetFullScreen;
  m_isInitialSetFullScreen = false;

  // Need to return true
  // * when this SetFullScreen() call was initiated by a configure() event
  // * on first SetFullScreen so GraphicsContext gets resolution
  // Otherwise, Kodi must keep the old resolution.
  return wasConfigure || wasInitialSetFullScreen;
}

void CWinSystemWayland::HandleSurfaceConfigure(std::uint32_t serial, std::int32_t width, std::int32_t height)
{
  CSingleLock lock(g_graphicsContext);
  CLog::LogF(LOGDEBUG, "Configure serial %u: size %dx%d", serial, width, height);
  m_currentConfigureSerial = serial;
  if (!ResetSurfaceSize(width, height, m_scale))
  {
    // nothing changed, ack immediately
    AckConfigure(serial);
  }
  // configure is acked when the Kodi surface has actually been reconfigured
}

void CWinSystemWayland::AckConfigure(std::uint32_t serial)
{
  // Send ack if we have a new serial number or this is the first time
  // this function is called
  if (serial != m_lastAckedSerial || !m_firstSerialAcked)
  {
    CLog::LogF(LOGDEBUG, "Acking serial %u", serial);
    m_shellSurface->AckConfigure(serial);
    m_lastAckedSerial = serial;
    m_firstSerialAcked = true;
  }
}

/**
 * Set the internal surface size variables and perform resolution change
 *
 * Call only from Wayland event processing thread!
 *
 * \return Whether surface parameters changed and video resolution change was
 *         performed
 */
bool CWinSystemWayland::ResetSurfaceSize(std::int32_t width, std::int32_t height, std::int32_t scale)
{
  // Wayland will tell us here the size of the surface that was actually created,
  // which might be different from what we expected e.g. when fullscreening
  // on an output we chose - the compositor might have decided to use a different
  // output for example
  // It is very important that the EGL native module and the rendering system use the
  // Wayland-announced size for rendering or corrupted graphics output will result.

  RESOLUTION switchToRes = RES_INVALID;

  // FIXME See comment in SetFullScreen
  CSingleLock lock(g_graphicsContext);

  // Now update actual resolution with configured one
  bool scaleChanged = (scale != m_scale);
  m_scale = scale;
  bool sizeChanged = SetSizeFromSurfaceSize(width, height);
 
  // Get actual frame rate from monitor, take highest frame rate if multiple
  // m_surfaceOutputs is only updated from event handling thread, so no lock
  auto maxRefreshIt = std::max_element(m_surfaceOutputs.cbegin(), m_surfaceOutputs.cend(), OutputCurrentRefreshRateComparer());
  float refreshRate = m_fRefreshRate;
  if (maxRefreshIt != m_surfaceOutputs.cend())
  {
    refreshRate = (*maxRefreshIt)->GetCurrentMode().refreshMilliHz / 1000.0f;
    CLog::LogF(LOGDEBUG, "Resolved actual (maximum) refresh rate to %.3f Hz on output \"%s\"", refreshRate, UserFriendlyOutputName(*maxRefreshIt).c_str());
  }

  if (refreshRate == m_fRefreshRate && !scaleChanged && !sizeChanged)
  {
    CLog::LogF(LOGDEBUG, "No change in size, refresh rate, and scale, returning");
    return false;
  }

  m_fRefreshRate = refreshRate;

  // Find matching Kodi resolution member
  switchToRes = FindMatchingCustomResolution(m_nWidth, m_nHeight, m_fRefreshRate);

  if (switchToRes == RES_INVALID)
  {
    // Add new resolution if none found
    RESOLUTION_INFO newResInfo;
    UpdateDesktopResolution(newResInfo, 0, m_nWidth, m_nHeight, m_fRefreshRate);
    newResInfo.strOutput = m_currentOutput; // we just assume the compositor put us on the right output
    CDisplaySettings::GetInstance().AddResolutionInfo(newResInfo);
    CDisplaySettings::GetInstance().ApplyCalibrations();
    switchToRes = static_cast<RESOLUTION> (CDisplaySettings::GetInstance().ResolutionInfoSize() - 1);
  }

  // RES_DESKTOP does not change usually, it is still the current resolution
  // of the selected output

  assert(switchToRes != RES_INVALID);

  // Mark resolution so that we know it came from configure
  CDisplaySettings::GetInstance().GetResolutionInfo(switchToRes).strId = CONFIGURE_RES_ID;

  CSingleExit exit(g_graphicsContext);

  // Force resolution update
  // SetVideoResolution() automatically delegates to main thread via internal
  // message if called from other threads
  // This will call SetFullScreen() with the new resolution, which also updates
  // the size of the egl_window etc. from m_nWidth/m_nHeight.
  // The call always blocks, so the configuration lock must be released beforehand.
  // FIXME Ideally this class would be completely decoupled from g_graphicsContext,
  // but this is not possible at the moment before the refactoring is done.
  g_graphicsContext.SetVideoResolution(switchToRes, true);

  return true;
}

/**
 * Calculate internal resolution from surface size and set variables
 *
 * \return whether any size variable changed
 */
bool CWinSystemWayland::SetSizeFromSurfaceSize(std::int32_t surfaceWidth, std::int32_t surfaceHeight)
{
  std::int32_t newWidth = surfaceWidth * m_scale;
  std::int32_t newHeight = surfaceHeight * m_scale;

  if (surfaceWidth != m_surfaceWidth || surfaceHeight != m_surfaceHeight || newWidth != m_nWidth || newHeight != m_nHeight)
  {
    m_surfaceWidth = surfaceWidth;
    m_surfaceHeight = surfaceHeight;
    m_nWidth = newWidth;
    m_nHeight = newHeight;
    CLog::LogF(LOGINFO, "Set surface size %dx%d at scale %d -> resolution %dx%d", m_surfaceWidth, m_surfaceHeight, m_scale, m_nWidth, m_nHeight);
    return true;
  }
  else
  {
    return false;
  }
}

std::string CWinSystemWayland::UserFriendlyOutputName(std::shared_ptr<COutput> const& output)
{
  std::vector<std::string> parts;
  if (!output->GetMake().empty())
  {
    parts.emplace_back(output->GetMake());
  }
  if (!output->GetModel().empty())
  {
    parts.emplace_back(output->GetModel());
  }
  if (parts.empty())
  {
    // Fallback to "unknown" if no name received from compositor
    parts.emplace_back(g_localizeStrings.Get(13205));
  }

  // Add position
  std::int32_t x, y;
  std::tie(x, y) = output->GetPosition();
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
  auto newSeatEmplace = m_seatProcessors.emplace(std::piecewise_construct, std::forward_as_tuple(name), std::forward_as_tuple(name, seat, this));
  newSeatEmplace.first->second.SetCoordinateScale(m_scale);
}

void CWinSystemWayland::OnOutputAdded(std::uint32_t name, wayland::output_t& output)
{
  // This is not accessed from multiple threads
  m_outputsInPreparation.emplace(name, new COutput(name, output, std::bind(&CWinSystemWayland::OnOutputDone, this, name)));
}

void CWinSystemWayland::OnOutputDone(std::uint32_t name)
{
  auto it = m_outputsInPreparation.find(name);
  if (it != m_outputsInPreparation.end())
  {
    // This output was added for the first time - done is also sent when
    // output parameters change later

    {
      CSingleLock lock(m_outputsMutex);
      // Move from m_outputsInPreparation to m_outputs
      m_outputs.emplace(std::move(*it));
      m_outputsInPreparation.erase(it);
    }

    // Maybe the output that was added was the one we should be on?
    if (m_bFullScreen)
    {
      CSingleLock lock(g_graphicsContext);
      UpdateResolutions();
      // This will call SetFullScreen(), which will match the output against
      // the information from the resolution and call set_fullscreen on the
      // surface if it changed.
      g_graphicsContext.SetVideoResolution(g_graphicsContext.GetVideoResolution(), true);
    }
  }

  UpdateBufferScale();
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
      // Theoretically, the compositor should automatically put us on another
      // (visible and connected) output if the output we were on is lost,
      // so there is nothing in particular to do here
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

void CWinSystemWayland::UpdateBufferScale()
{
  // Adjust our surface size to the output with the biggest scale in order
  // to get the best quality
  auto const maxBufferScaleIt = std::max_element(m_surfaceOutputs.cbegin(), m_surfaceOutputs.cend(), OutputScaleComparer());
  if (maxBufferScaleIt != m_surfaceOutputs.cend())
  {
    auto const newScale = (*maxBufferScaleIt)->GetScale();
    // Recalculate resolution with new scale if it changed
    ResetSurfaceSize(m_surfaceWidth, m_surfaceHeight, newScale);
  }
}

void CWinSystemWayland::ApplyBufferScale(std::int32_t scale)
{
  CLog::LogF(LOGINFO, "Setting Wayland buffer scale to %d", scale);
  m_surface.set_buffer_scale(scale);
  CSingleLock lock(m_seatProcessorsMutex);
  for (auto& seatProcessor : m_seatProcessors)
  {
    seatProcessor.second.SetCoordinateScale(scale);
  }
}

#if defined(HAVE_LIBVA)
void* CWinSystemWayland::GetVaDisplay()
{
  return vaGetDisplayWl(reinterpret_cast<wl_display*> (m_connection->GetDisplay().c_ptr()));
}
#endif
