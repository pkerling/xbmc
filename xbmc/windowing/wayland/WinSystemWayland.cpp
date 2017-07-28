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
#include <numeric>

#if defined(HAVE_LIBVA)
#include <va/va_wayland.h>
#endif

#include "Application.h"
#include "Connection.h"
#include "cores/VideoPlayer/Process/wayland/ProcessInfoWayland.h"
#include "guilib/DispResource.h"
#include "guilib/GraphicContext.h"
#include "guilib/LocalizeStrings.h"
#include "input/InputManager.h"
#include "linux/TimeUtils.h"
#include "input/touch/generic/GenericTouchActionHandler.h"
#include "input/touch/generic/GenericTouchInputHandler.h"
#include "linux/PlatformConstants.h"
#include "messaging/ApplicationMessenger.h"
#include "OSScreenSaverIdleInhibitUnstableV1.h"
#include "Registry.h"
#include "ServiceBroker.h"
#include "settings/AdvancedSettings.h"
#include "settings/DisplaySettings.h"
#include "settings/Settings.h"
#include "ShellSurfaceWlShell.h"
#include "ShellSurfaceXdgShellUnstableV6.h"
#include "threads/SingleLock.h"
#include "utils/log.h"
#include "utils/MathUtils.h"
#include "utils/StringUtils.h"
#include "VideoSyncWpPresentation.h"
#include "WindowDecorator.h"
#include "WinEventsWayland.h"
#include "utils/TimeUtils.h"

#if defined(HAVE_DBUS)
# include "windowing/linux/OSScreenSaverFreedesktop.h"
#endif

using namespace KODI::WINDOWING;
using namespace KODI::WINDOWING::WAYLAND;
using namespace std::placeholders;

namespace
{

RESOLUTION FindMatchingCustomResolution(CSizeInt size, float refreshRate)
{
  CSingleLock lock(g_graphicsContext);
  for (size_t res = RES_DESKTOP; res < CDisplaySettings::GetInstance().ResolutionInfoSize(); ++res)
  {
    auto const& resInfo = CDisplaySettings::GetInstance().GetResolutionInfo(res);
    if (resInfo.iWidth == size.Width() && resInfo.iHeight == size.Height() && MathUtils::FloatEquals(resInfo.fRefreshRate, refreshRate, 0.0005f))
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

/// ID for a resolution that was set in response to configure
const std::string CONFIGURE_RES_ID = "configure";
/// ID for a resoultion that was set in response to an internal event (such as setting window size explicitly)
const std::string INTERNAL_RES_ID = "internal";

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

  VIDEOPLAYER::CProcessInfoWayland::Register();

  CLog::LogFunction(LOGINFO, "CWinSystemWayland::InitWindowSystem", "Connecting to Wayland server");
  m_connection.reset(new CConnection());
  m_registry.reset(new CRegistry(*m_connection));

  m_registry->RequestSingleton(m_compositor, 1, 4);
  m_registry->RequestSingleton(m_shm, 1, 1);
  m_registry->RequestSingleton(m_presentation, 1, 1, false);
  // version 2 adds done() -> required
  // version 3 adds destructor -> optional
  m_registry->Request<wayland::output_t>(2, 3, std::bind(&CWinSystemWayland::OnOutputAdded, this, _1, _2), std::bind(&CWinSystemWayland::OnOutputRemoved, this, _1));

  m_registry->Bind();

  if (m_presentation)
  {
    m_presentation.on_clock_id() = [this](std::uint32_t clockId)
    {
      CLog::Log(LOGINFO, "Wayland presentation clock: %" PRIu32, clockId);
      m_presentationClock = static_cast<clockid_t> (clockId);
    };
  }

  // Do another roundtrip to get initial wl_output information
  m_connection->GetDisplay().roundtrip();
  if (m_outputs.empty())
  {
    throw std::runtime_error("No outputs received from compositor");
  }

  // Event loop is started in CreateWindow

  // pointer is by default not on this window, will be immediately rectified
  // by the enter() events if it is
  CServiceBroker::GetInputManager().SetMouseActive(false);
  // Always use the generic touch action handler
  CGenericTouchInputHandler::GetInstance().RegisterHandler(&CGenericTouchActionHandler::GetInstance());

  return CWinSystemBase::InitWindowSystem();
}

bool CWinSystemWayland::DestroyWindowSystem()
{
  // Make sure no more events get processed when we kill the instances
  CWinEventsWayland::SetDisplay(nullptr);

  m_windowDecorator.reset();
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
  m_surfaceSubmissions.clear();

  if (m_registry)
  {
    m_registry->UnbindSingletons();
  }
  m_registry.reset();
  m_seatRegistry.reset();
  m_connection.reset();

  CGenericTouchInputHandler::GetInstance().UnregisterHandler();

  return CWinSystemBase::DestroyWindowSystem();
}

bool CWinSystemWayland::CreateNewWindow(const std::string& name,
                                        bool fullScreen,
                                        RESOLUTION_INFO& res)
{
  m_surface = m_compositor.create_surface();
  m_surface.on_enter() = [this](wayland::output_t wloutput)
  {
    if (auto output = FindOutputByWaylandOutput(wloutput))
    {
      CLog::Log(LOGDEBUG, "Entering output \"%s\" with scale %d and %.3f dpi", UserFriendlyOutputName(output).c_str(), output->GetScale(), output->GetCurrentDpi());
      m_surfaceOutputs.emplace(output);
      UpdateBufferScale();
      UpdateTouchDpi();
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
      UpdateTouchDpi();
    }
    else
    {
      CLog::Log(LOGWARNING, "Leaving output that was not configured yet, ignoring");
    }
  };

  m_windowDecorator.reset(new CWindowDecorator(*this, *m_connection, m_surface));

  if (fullScreen)
  {
    m_shellSurfaceState.set(IShellSurface::STATE_FULLSCREEN);
  }
  // Assume we're active on startup until someone tells us otherwise
  m_shellSurfaceState.set(IShellSurface::STATE_ACTIVATED);
  // Try with this resolution if compositor does not say otherwise
  SetSize({res.iWidth, res.iHeight}, m_shellSurfaceState, false);

  m_shellSurface.reset(CShellSurfaceXdgShellUnstableV6::TryCreate(*m_connection, m_surface, name, KODI::LINUX::DESKTOP_FILE_NAME));
  if (!m_shellSurface)
  {
    CLog::LogF(LOGWARNING, "Compositor does not support xdg_shell unstable v6 protocol - falling back to wl_shell, not all features might work");
    m_shellSurface.reset(new CShellSurfaceWlShell(*m_connection, m_surface, name, KODI::LINUX::DESKTOP_FILE_NAME));
  }

  // Just remember initial width/height for context creation
  // This is used for sizing the EGLSurface
  m_shellSurface->OnConfigure() = [this](std::uint32_t serial, CSizeInt size, IShellSurface::StateBitset state)
  {
    if (!size.IsZero())
    {
      CLog::Log(LOGINFO, "Got initial Wayland surface size %dx%d", size.Width(), size.Height());
      SetSize(size, state, true);
    }
    m_shellSurfaceState = state;
    AckConfigure(serial);
  };

  if (fullScreen)
  {
    // Try to start on correct monitor and with correct buffer scale
    auto output = FindOutputByUserFriendlyName(CServiceBroker::GetSettings().GetString(CSettings::SETTING_VIDEOSCREEN_MONITOR));
    if (output)
    {
      m_shellSurface->SetFullScreen(output->GetWaylandOutput(), res.fRefreshRate);
      if (m_surface.can_set_buffer_scale())
      {
        m_scale = output->GetScale();
        ApplyBufferScale(m_scale);
      }
    }
  }

  m_shellSurface->Initialize();

  // Apply window decorations if necessary
  m_windowDecorator->SetState(m_configuredSize, m_scale, m_shellSurfaceState);

  // Update resolution with real size as it could have changed due to configure()
  UpdateDesktopResolution(res, 0, m_nWidth, m_nHeight, res.fRefreshRate);
  res.bFullScreen = fullScreen;

  // Set real handler during runtime
  m_shellSurface->OnConfigure() = std::bind(&CWinSystemWayland::HandleSurfaceConfigure, this, _1, _2, _3);

  m_seatRegistry.reset(new CRegistry(*m_connection));
  // version 2 adds name event -> optional
  // version 4 adds wl_keyboard repeat_info -> optional
  // version 5 adds discrete axis events in wl_pointer -> unused
  m_seatRegistry->Request<wayland::seat_t>(1, 5, std::bind(&CWinSystemWayland::OnSeatAdded, this, _1, _2), std::bind(&CWinSystemWayland::OnSeatRemoved, this, _1));
  m_seatRegistry->Bind();

  if (m_seatProcessors.empty())
  {
    CLog::Log(LOGWARNING, "Wayland compositor did not announce a wl_seat - you will not have any input devices for the time being");
  }

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
  return true;
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
  CLog::LogF(LOGINFO, "User wanted output \"%s\", we now have \"%s\" size %dx%d mm with %zu mode(s):", userOutput.c_str(), outputName.c_str(), physicalSize.Width(), physicalSize.Height(), modes.size());

  for (auto const& mode : modes)
  {
    bool isCurrent = (mode == currentMode);
    float pixelRatio = output->GetPixelRatioForMode(mode);
    CLog::LogF(LOGINFO, "- %dx%d @%.3f Hz pixel ratio %.3f%s", mode.size.Width(), mode.size.Height(), mode.refreshMilliHz / 1000.0f, pixelRatio, isCurrent ? " current" : "");

    RESOLUTION_INFO res;
    UpdateDesktopResolution(res, 0, mode.size.Width(), mode.size.Height(), mode.GetRefreshInHz());
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

bool CWinSystemWayland::ResizeWindow(int newWidth, int newHeight, int newLeft, int newTop)
{
  // CGraphicContext is "smart" and never calls SetFullScreen when we start in
  // windowed mode or already are in windowed mode and only have a resize.
  // But we need the call to happen since we can handle all of that ourselves,
  // SetFullScreen already takes care of all cases and updates egl_window/
  // rendersystem size etc.
  // Introducing another function for changing resolution state would just
  // complicate things - so defer to SetFullScreen anyway.
  auto& res = CDisplaySettings::GetInstance().GetResolutionInfo(RES_WINDOW);
  // The newWidth/newHeight parameters are taken from RES_WINDOW anyway, so we can just
  // ignore them
  return SetFullScreen(false, res, false);
}

bool CWinSystemWayland::SetFullScreen(bool fullScreen, RESOLUTION_INFO& res, bool blankOtherDisplays)
{
  // FIXME Our configuration is protected by graphicsContext lock
  // If we'd use a mutex private to this class, we would have to lock both
  // that one and graphicsContext (because the resolutions get updated),
  // leading to a possible deadlock.
  CSingleLock lock(g_graphicsContext);

  // In fullscreen modes, we never change the surface size on Kodi's request,
  // but only when the compositor tells us to. At least xdg_shell specifies
  // that with state fullscreen the dimensions given in configure() must
  // always be observed.
  // This does mean that the compositor has no way of knowing which resolution
  // we would (in theory) want. Since no compositor implements dynamic resolution
  // switching at the moment, this is not a problem. If it is some day implemented
  // in compositors, this code must be changed to match the behavior that is
  // expected then anyway.

  // We can honor the Kodi-requested size only if we are not bound by configure rules,
  // which applies for maximized and fullscreen states.
  // Also, setting an unconfigured size just when going fullscreen makes no sense.
  bool mustHonorSize = m_shellSurfaceState.test(IShellSurface::STATE_MAXIMIZED) || m_shellSurfaceState.test(IShellSurface::STATE_FULLSCREEN) || fullScreen;

  bool wasConfigure = (res.strId == CONFIGURE_RES_ID);
  bool wasInternal = (res.strId == INTERNAL_RES_ID);
  // Everything that has not a known ID came from other sources
  bool wasExternal = !wasConfigure && !wasInternal;
  // Reset configure flag
  // Setting it in res will not modify the global information in CDisplaySettings
  // and we don't know which resolution index this is, so just reset all
  for (size_t resIdx = RES_DESKTOP; resIdx < CDisplaySettings::GetInstance().ResolutionInfoSize(); resIdx++)
  {
    CDisplaySettings::GetInstance().GetResolutionInfo(resIdx).strId = "";
  }

  CLog::LogF(LOGINFO, "Wayland asked to switch mode to %dx%d @%.3f Hz on output \"%s\" %s%s%s", res.iWidth, res.iHeight, res.fRefreshRate, res.strOutput.c_str(), fullScreen ? "full screen" : "windowed", wasConfigure ? " from configure" : "", wasInternal ? " internally" : "");

  if (fullScreen)
  {
    if (wasExternal || m_currentOutput != res.strOutput)
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
      CLog::LogF(LOGDEBUG, "Called internally, not calling SetFullscreen on surface");
    }
  }
  else
  {
    if (wasExternal)
    {
      if (m_shellSurfaceState.test(IShellSurface::STATE_FULLSCREEN))
      {
        CLog::LogF(LOGDEBUG, "Setting windowed");
        m_shellSurface->SetWindowed();
      }
      else
      {
        CLog::LogF(LOGDEBUG, "Not setting windowed: already windowed");
      }
    }
    else
    {
      CLog::LogF(LOGDEBUG, "Not setting windowed: called internally");
    }
  }

  if (wasExternal && !mustHonorSize)
  {
    CLog::LogF(LOGDEBUG, "Directly setting windowed size %dx%d on Kodi request", res.iWidth, res.iHeight);
    // Kodi is directly setting window size, apply
    SetSize({res.iWidth, res.iHeight}, m_shellSurfaceState, false);
  }

  // Go ahead if
  // * the request came from inside WinSystemWayland (for a compositor configure
  //   request or because we want to resize the window explicitly) or
  // * we are free to choose any size we want, which means we do not have to wait
  //   for configure
  if (!wasExternal || !mustHonorSize)
  {
    // Mark everything opaque so the compositor can render it faster
    // Do it here so size always matches the configured egl surface
    CLog::LogF(LOGDEBUG, "Setting opaque region size %dx%d", m_surfaceSize.Width(), m_surfaceSize.Height());
    wayland::region_t opaqueRegion = m_compositor.create_region();
    opaqueRegion.add(0, 0, m_surfaceSize.Width(), m_surfaceSize.Height());
    m_surface.set_opaque_region(opaqueRegion);
    if (m_surface.can_set_buffer_scale())
    {
      // Buffer scale must also match egl size configuration
      ApplyBufferScale(m_scale);
    }

    // FIXME This assumes that the resolution has already been set. Should
    // be moved to some post-change callback when resolution setting is refactored.
    if (!m_inhibitSkinReload)
    {
      g_application.ReloadSkin();
    }

    // Next buffer that the graphic context attaches will have the size corresponding
    // to this configure, so go and ack it
    if (wasConfigure)
    {
      // New shell surface state can only come from configure
      m_shellSurfaceState = m_nextShellSurfaceState;
      m_windowDecorator->SetState(m_configuredSize, m_scale, m_shellSurfaceState);
      AckConfigure(m_currentConfigureSerial);
    }
  }

  bool wasInitialSetFullScreen = m_isInitialSetFullScreen;
  m_isInitialSetFullScreen = false;

  // Need to return true
  // * when this SetFullScreen() call was initiated by a configure() event
  // * on first SetFullScreen so GraphicsContext gets resolution
  // Otherwise, Kodi must keep the old resolution.
  return wasConfigure || wasInitialSetFullScreen;
}


void CWinSystemWayland::SetInhibitSkinReload(bool inhibit)
{
  m_inhibitSkinReload = inhibit;
  if (!inhibit)
  {
    g_application.ReloadSkin();
  }
}
void CWinSystemWayland::HandleSurfaceConfigure(std::uint32_t serial, CSizeInt size, IShellSurface::StateBitset state)
{
  CSingleLock lock(g_graphicsContext);
  CLog::LogF(LOGDEBUG, "Configure serial %u: size %dx%d state %s", serial, size.Width(), size.Height(), IShellSurface::StateToString(state).c_str());
  m_currentConfigureSerial = serial;
  m_nextShellSurfaceState = state;
  if (!ResetSurfaceSize(size, m_scale, state.test(IShellSurface::STATE_FULLSCREEN), true))
  {
    // surface state may still have changed, apply that
    // Fullscreen will not have changed, since that is handled in ResetSurfaceSize.
    // All other changes only affect the appearance of the decorations and so need
    // not be synchronized with the main surface.
    // FIXME Call from main thread!!
    m_shellSurfaceState = m_nextShellSurfaceState;
    if (m_windowDecorator)
    {
      m_windowDecorator->SetState(m_configuredSize, m_scale, m_shellSurfaceState);
    }
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
 * size can be zero if compositor does not have a preference
 *
 * \return Whether surface parameters changed and video resolution change was
 *         performed
 */
bool CWinSystemWayland::ResetSurfaceSize(CSizeInt size, std::int32_t scale, bool fullScreen, bool fromConfigure)
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

  bool sizeChanged = false;
  bool sizeIncludesDecoration = true;

  if (size.IsZero())
  {
    if (fullScreen)
    {
      // Do not change current size - SetSize must be called regardless in case
      // scale or something else changed
      size = m_configuredSize;
    }
    else
    {
      // Compositor has no preference and we're going windowed
      // -> adopt windowed size that Kodi wants
      auto const& windowed = CDisplaySettings::GetInstance().GetResolutionInfo(RES_WINDOW);
      size = CSizeInt{windowed.iWidth, windowed.iHeight};
      sizeIncludesDecoration = false;
    }
  }

  sizeChanged = SetSize(size, m_nextShellSurfaceState, sizeIncludesDecoration);
 
  // Get actual frame rate from monitor, take highest frame rate if multiple
  // m_surfaceOutputs is only updated from event handling thread, so no lock
  auto maxRefreshIt = std::max_element(m_surfaceOutputs.cbegin(), m_surfaceOutputs.cend(), OutputCurrentRefreshRateComparer());
  float refreshRate = m_fRefreshRate;
  if (maxRefreshIt != m_surfaceOutputs.cend())
  {
    refreshRate = (*maxRefreshIt)->GetCurrentMode().GetRefreshInHz();
    CLog::LogF(LOGDEBUG, "Resolved actual (maximum) refresh rate to %.3f Hz on output \"%s\"", refreshRate, UserFriendlyOutputName(*maxRefreshIt).c_str());
  }

  if (refreshRate == m_fRefreshRate && !scaleChanged && !sizeChanged && m_bFullScreen == fullScreen)
  {
    CLog::LogF(LOGDEBUG, "No change in size, refresh rate, scale, and fullscreen state, returning");
    return false;
  }

  m_fRefreshRate = refreshRate;
  m_bFullScreen = fullScreen;

  // Find matching Kodi resolution member
  if (!m_bFullScreen)
  {
    switchToRes = RES_WINDOW;
    SetWindowResolution(m_nWidth, m_nHeight);
    // Save window size
    CServiceBroker::GetSettings().SetInt(CSettings::SETTING_WINDOW_WIDTH, m_nWidth);
    CServiceBroker::GetSettings().SetInt(CSettings::SETTING_WINDOW_HEIGHT, m_nHeight);
    CServiceBroker::GetSettings().Save();
  }
  else
  {
    switchToRes = FindMatchingCustomResolution({m_nWidth, m_nHeight}, m_fRefreshRate);
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
  }

  // RES_DESKTOP does not change usually, it is still the current resolution
  // of the selected output

  assert(switchToRes != RES_INVALID);

  // Mark resolution so that we know it came from configure
  CDisplaySettings::GetInstance().GetResolutionInfo(switchToRes).strId = fromConfigure ? CONFIGURE_RES_ID : INTERNAL_RES_ID;

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
 * \param next surface size
 * \param state window state to determine whether decorations are enabled at all
 * \param sizeIncludesDecoration if true, given size includes potential window decorations
 * \return whether main buffer (not surface) size changed
 */
bool CWinSystemWayland::SetSize(CSizeInt size, IShellSurface::StateBitset state, bool sizeIncludesDecoration)
{
  // Depending on whether the size has decorations included (i.e. comes from the
  // compositor or from Kodi), we need to calculate differently
  if (sizeIncludesDecoration)
  {
    m_configuredSize = size;
    m_surfaceSize = m_windowDecorator->CalculateMainSurfaceSize(size, state);
  }
  else
  {
    m_surfaceSize = size;
    m_configuredSize = m_windowDecorator->CalculateFullSurfaceSize(size, state);
  }
  CSizeInt newSize{m_surfaceSize * m_scale};

  if (newSize.Width() != m_nWidth || newSize.Height() != m_nHeight)
  {
    m_nWidth = newSize.Width();
    m_nHeight = newSize.Height();
    CLog::LogF(LOGINFO, "Set size %dx%d %s decoration at scale %d -> configured size %dx%d, surface size %dx%d, resolution %dx%d", size.Width(), size.Height(), sizeIncludesDecoration ? "including" : "excluding", m_scale, m_configuredSize.Width(), m_configuredSize.Height(), m_surfaceSize.Width(), m_surfaceSize.Height(), m_nWidth, m_nHeight);
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
  auto pos = output->GetPosition();
  if (pos.x != 0 || pos.y != 0)
  {
    parts.emplace_back(StringUtils::Format("@{}x{}", pos.x, pos.y));
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
    m_cursorTheme = wayland::cursor_theme_t("", 16, m_shm);
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
    m_cursorSurface = m_compositor.create_surface();
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

void CWinSystemWayland::OnSeatAdded(std::uint32_t name, wayland::proxy_t&& proxy)
{
  CSingleLock lock(m_seatProcessorsMutex);

  wayland::seat_t seat(proxy);
  auto newSeatEmplace = m_seatProcessors.emplace(std::piecewise_construct,
                                                 std::forward_as_tuple(name),
                                                 std::forward_as_tuple(name, seat, m_surface, *m_connection, static_cast<IInputHandler&> (*this)));
  newSeatEmplace.first->second.SetCoordinateScale(m_scale);
}

void CWinSystemWayland::OnSeatRemoved(std::uint32_t name)
{
  CSingleLock lock(m_seatProcessorsMutex);

  m_seatProcessors.erase(name);
}

void CWinSystemWayland::OnOutputAdded(std::uint32_t name, wayland::proxy_t&& proxy)
{
  wayland::output_t output(proxy);
  // This is not accessed from multiple threads
  m_outputsInPreparation.emplace(name, std::make_shared<COutput>(name, output, std::bind(&CWinSystemWayland::OnOutputDone, this, name)));
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

void CWinSystemWayland::OnOutputRemoved(std::uint32_t name)
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
    CServiceBroker::GetInputManager().SetMouseActive(true);
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
    CServiceBroker::GetInputManager().SetMouseActive(false);
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
  if (!m_surface || !m_surface.can_set_buffer_scale())
  {
    // Never modify scale when we cannot set it
    return;
  }

  // Adjust our surface size to the output with the biggest scale in order
  // to get the best quality
  auto const maxBufferScaleIt = std::max_element(m_surfaceOutputs.cbegin(), m_surfaceOutputs.cend(), OutputScaleComparer());
  if (maxBufferScaleIt != m_surfaceOutputs.cend())
  {
    auto const newScale = (*maxBufferScaleIt)->GetScale();
    // Recalculate resolution with new scale if it changed
    ResetSurfaceSize(m_configuredSize, newScale, m_bFullScreen, false);
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

void CWinSystemWayland::UpdateTouchDpi()
{
  // If we have multiple outputs with wildly different DPI, this is really just
  // guesswork to get a halfway reasonable value. min/max would probably also be OK.
  float dpiSum = std::accumulate(m_surfaceOutputs.cbegin(), m_surfaceOutputs.cend(),  0.0f,
                                 [](float acc, std::shared_ptr<COutput> const& output)
                                 {
                                   return acc + output->GetCurrentDpi();
                                 });
  float dpi = dpiSum / m_surfaceOutputs.size();
  CLog::LogF(LOGDEBUG, "Computed average dpi of %.3f for touch handler", dpi);
  CGenericTouchInputHandler::GetInstance().SetScreenDPI(dpi);
}

CWinSystemWayland::SurfaceSubmission::SurfaceSubmission(timespec const& submissionTime, wayland::presentation_feedback_t const& feedback)
: submissionTime{submissionTime}, feedback{feedback}
{
}

timespec CWinSystemWayland::GetPresentationClockTime()
{
  timespec time;
  if (clock_gettime(m_presentationClock, &time) != 0)
  {
    throw std::system_error(errno, std::generic_category(), "Error getting time from Wayland presentation clock with clock_gettime");
  }
  return time;
}

void CWinSystemWayland::PrepareFramePresentation()
{
  // Continuously measure display latency (i.e. time between when the frame was rendered
  // and when it becomes visible to the user) to correct AV sync
  if (m_presentation)
  {
    auto tStart = GetPresentationClockTime();
    // wp_presentation_feedback creation is coupled to the surface's commit().
    // eglSwapBuffers() (which will be called after this) will call commit().
    // This creates a new Wayland protocol object in the main thread, but this
    // will not result in a race since the corresponding events are never sent
    // before commit() on the surface, which only occurs afterwards.
    auto feedback = m_presentation.feedback(m_surface);
    // Save feedback objects in list so they don't get destroyed upon exit of this function
    // Hand iterator to lambdas so they do not hold a (then circular) reference
    // to the actual object
    decltype(m_surfaceSubmissions)::iterator iter;
    {
      CSingleLock lock(m_surfaceSubmissionsMutex);
      iter = m_surfaceSubmissions.emplace(m_surfaceSubmissions.end(), tStart, feedback);
    }

    feedback.on_sync_output() = [this](wayland::output_t wloutput)
    {
      m_syncOutputID = wloutput.get_id();
      auto output = FindOutputByWaylandOutput(wloutput);
      if (output)
      {
        m_syncOutputRefreshRate = output->GetCurrentMode().GetRefreshInHz();
      }
      else
      {
        CLog::Log(LOGWARNING, "Could not find Wayland output that is supposedly the sync output");
      }
    };
    feedback.on_presented() = [this,iter](std::uint32_t tvSecHi, std::uint32_t tvSecLo, std::uint32_t tvNsec, std::uint32_t refresh, std::uint32_t seqHi, std::uint32_t seqLo, wayland::presentation_feedback_kind flags)
    {
      timespec tv = { .tv_sec = static_cast<std::time_t> ((static_cast<std::uint64_t>(tvSecHi) << 32) + tvSecLo), .tv_nsec = tvNsec };
      std::int64_t latency = KODI::LINUX::TimespecDifference(iter->submissionTime, tv);
      std::uint64_t msc = (static_cast<std::uint64_t>(seqHi) << 32) + seqLo;
      m_presentationFeedbackHandlers.Invoke(tv, refresh, m_syncOutputID, m_syncOutputRefreshRate, msc);

      iter->latency = latency / 1e9f; // nanoseconds to seconds
      float adjust{};
      {
        CSingleLock lock(m_surfaceSubmissionsMutex);
        if (m_surfaceSubmissions.size() > LATENCY_MOVING_AVERAGE_SIZE)
        {
          adjust = - m_surfaceSubmissions.front().latency / LATENCY_MOVING_AVERAGE_SIZE;
          m_surfaceSubmissions.pop_front();
        }
      }
      m_latencyMovingAverage = m_latencyMovingAverage + iter->latency / LATENCY_MOVING_AVERAGE_SIZE + adjust;

      if (g_advancedSettings.CanLogComponent(LOGAVTIMING))
      {
        CLog::Log(LOGDEBUG, "Presentation feedback: %" PRIi64 " ns -> moving average %f s", latency, static_cast<double> (m_latencyMovingAverage));
      }
    };
    feedback.on_discarded() = [this,iter]()
    {
      CLog::Log(LOGDEBUG, "Presentation: Frame was discarded by compositor");
      CSingleLock lock(m_surfaceSubmissionsMutex);
      m_surfaceSubmissions.erase(iter);
    };
  }
}

void CWinSystemWayland::FinishFramePresentation()
{
  m_frameStartTime = CurrentHostCounter();
}

float CWinSystemWayland::GetFrameLatencyAdjustment()
{
  std::uint64_t now = CurrentHostCounter();
  return static_cast<float> (now - m_frameStartTime) / CurrentHostFrequency() * 1000.0f;
}

float CWinSystemWayland::GetDisplayLatency()
{
  if (m_presentation)
  {
    return m_latencyMovingAverage * 1000.0f;
  }
  else
  {
    return CWinSystemBase::GetDisplayLatency();
  }
}

float CWinSystemWayland::GetSyncOutputRefreshRate()
{
  return m_syncOutputRefreshRate;
}

KODI::CSignalRegistration CWinSystemWayland::RegisterOnPresentationFeedback(PresentationFeedbackHandler handler)
{
  return m_presentationFeedbackHandlers.Register(handler);
}

std::unique_ptr<CVideoSync> CWinSystemWayland::GetVideoSync(void* clock)
{
  if (m_surface && m_presentation)
  {
    CLog::LogF(LOGINFO, "Using presentation protocol for video sync");
    return std::unique_ptr<CVideoSync>(new CVideoSyncWpPresentation(clock));
  }
  else
  {
    CLog::LogF(LOGINFO, "No supported method for video sync found");
    return nullptr;
  }
}

#if defined(HAVE_LIBVA)
void* CWinSystemWayland::GetVaDisplay()
{
  return vaGetDisplayWl(m_connection->GetDisplay());
}
#endif

std::unique_ptr<IOSScreenSaver> CWinSystemWayland::GetOSScreenSaverImpl()
{
  if (m_surface)
  {
    std::unique_ptr<IOSScreenSaver> ptr;
    ptr.reset(COSScreenSaverIdleInhibitUnstableV1::TryCreate(*m_connection, m_surface));
    if (ptr)
    {
      CLog::LogF(LOGINFO, "Using idle-inhibit-unstable-v1 protocol for screen saver inhibition");
      return ptr;
    }
  }
#if defined(HAVE_DBUS)
  else if (KODI::WINDOWING::LINUX::COSScreenSaverFreedesktop::IsAvailable())
  {
    CLog::LogF(LOGINFO, "Using freedesktop.org DBus interface for screen saver inhibition");
    return std::unique_ptr<IOSScreenSaver>(new KODI::WINDOWING::LINUX::COSScreenSaverFreedesktop);
  }
#endif
  else
  {
    CLog::LogF(LOGINFO, "No supported method for screen saver inhibition found");
    return nullptr;
  }
}

std::string CWinSystemWayland::GetClipboardText()
{
  CSingleLock lock(m_seatProcessorsMutex);
  // Get text of first seat with non-empty selection
  // Actually, the value of the seat that received the Ctrl+V keypress should be used,
  // but this would need a workaround or proper multi-seat support in Kodi - it's
  // probably just not that relevant in practice
  for (auto const& seat : m_seatProcessors)
  {
    auto text = seat.second.GetSelectionText();
    if (text != "")
    {
      return text;
    }
  }
  return "";
}

/**
 * Apply queued surface state change and adjust main surface size if necessary
 *
 * Adds or removes window decorations as necessary. If window decorations are enabled,
 * the returned size will be the configured size decreased by the size of the
 * window decorations.
 *
 * \param state surface state to set
 * \param configuredSize size that the compositor has configured which includes window
 *                       decorations
 * \return size that should be used for the main surface
 */
CSizeInt CWinSystemWayland::ApplyShellSurfaceState(IShellSurface::StateBitset state, CSizeInt configuredSize)
{
  m_shellSurfaceState = state;
  m_windowDecorator->SetState(configuredSize, m_scale, state);
  return configuredSize;
}

void CWinSystemWayland::OnWindowMove(const wayland::seat_t& seat, std::uint32_t serial)
{
  m_shellSurface->StartMove(seat, serial);
}

void CWinSystemWayland::OnWindowResize(const wayland::seat_t& seat, std::uint32_t serial, wayland::shell_surface_resize edge)
{
  m_shellSurface->StartResize(seat, serial, edge);
}

void CWinSystemWayland::OnWindowShowContextMenu(const wayland::seat_t& seat, std::uint32_t serial, CPointInt position)
{
  m_shellSurface->ShowShellContextMenu(seat, serial, position);
}

void CWinSystemWayland::OnWindowClose()
{
  KODI::MESSAGING::CApplicationMessenger::GetInstance().PostMsg(TMSG_QUIT);
}

void CWinSystemWayland::OnWindowMinimize()
{
  m_shellSurface->SetMinimized();
}

void CWinSystemWayland::OnWindowMaximize()
{
  if (m_shellSurfaceState.test(IShellSurface::STATE_MAXIMIZED))
  {
    m_shellSurface->UnsetMaximized();
  }
  else
  {
    m_shellSurface->SetMaximized();
  }
}