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

#include "WinEventsWayland.h"

#include <unistd.h>
#include <sys/poll.h>

#include <exception>
#include <memory>
#include <system_error>

#include "Application.h"
#include "threads/SingleLock.h"
#include "threads/Thread.h"
#include "utils/log.h"

using namespace KODI::WINDOWING::WAYLAND;

namespace
{
/**
 * Thread for processing Wayland events
 * 
 * While not strictly needed, reading from the Wayland display file descriptor
 * and dispatching the resulting events is done in an extra thread here.
 * Sometime in the future, MessagePump() might be gone and then the
 * transition will be easier since this extra thread is already here.
 */
class CWinEventsWaylandThread : CThread
{
  wayland::display_t* m_display;
  // Pipe used for cancelling poll() on shutdown
  int m_pipe[2];

public:

  CWinEventsWaylandThread(wayland::display_t* display)
  : CThread("Wayland message pump"), m_display(display)
  {
    if (pipe(m_pipe) < 0)
    {
      throw std::system_error(errno, std::generic_category(), "Error creating pipe for Wayland message pump cancellation");
    }
    Create();
  }

  virtual ~CWinEventsWaylandThread()
  {
    close(m_pipe[0]);
    close(m_pipe[1]);
  }

  void Stop()
  {
    CLog::Log(LOGDEBUG, "Stopping Wayland message pump");
    char c = 0;
    write(m_pipe[1], &c, 1);
    WaitForThreadExit(0);
  }

private:

  void Process() override
  {
    try
    {
      std::array<pollfd, 2> pollFds;
      pollfd& waylandPoll = pollFds[0];
      pollfd& cancelPoll = pollFds[1];
      // Wayland filedescriptor
      waylandPoll.fd = m_display->get_fd();
      waylandPoll.events = POLLIN;
      waylandPoll.revents = 0;
      // Read end of the cancellation pipe
      cancelPoll.fd = m_pipe[0];
      cancelPoll.events = POLLIN;
      cancelPoll.revents = 0;

      CLog::Log(LOGDEBUG, "Starting Wayland message pump");

      // Run until cancelled or error
      while (true)
      {
        // dispatch() provides no way to cancel a blocked read from the socket
        // wl_display_disconnect would just close the socket, leading to problems
        // with the poll() that dispatch() uses internally - so we have to implement
        // cancellation ourselves here

        // Acquire global read intent
        wayland::read_intent readIntent = m_display->obtain_read_intent();
        m_display->flush();

        if (poll(pollFds.data(), pollFds.size(), -1) < 0)
        {
          throw std::system_error(errno, std::generic_category(), "Error polling on Wayland socket");
        }

        if (cancelPoll.revents & POLLIN || cancelPoll.revents & POLLERR || cancelPoll.revents & POLLHUP || cancelPoll.revents & POLLNVAL)
        {
          // We were cancelled, no need to dispatch events
          break;
        }

        if (waylandPoll.revents & POLLERR || waylandPoll.revents & POLLHUP || waylandPoll.revents & POLLNVAL)
        {
          throw std::runtime_error("poll() signalled error condition on Wayland socket");
        }

        // Read events and release intent; this does not block
        readIntent.read();
        // Dispatch default event queue
        if (m_display->dispatch_pending() < 0)
        {
          throw std::system_error(errno, std::generic_category(), "Error dispatching Wayland events");
        }
      }

      CLog::Log(LOGDEBUG, "Wayland message pump stopped");
    }
    catch (std::exception& e)
    {
      // FIXME CThread::OnException is very badly named and should probably go away
      // FIXME Thread exception handling is seriously broken:
      // Exceptions will be swallowed and do not terminate the program.
      // Even XbmcCommons::UncheckedException which claims to be there for just this
      // purpose does not cause termination, the log message will just be slightly different.
      
      // But here, going on would be meaningless, so do a hard exit
      CLog::Log(LOGFATAL, "Exception in Wayland message pump, exiting: %s", e.what());
      std::terminate();
    }
  }
};

std::unique_ptr<CWinEventsWaylandThread> g_WlMessagePump = nullptr;

}

void CWinEventsWayland::SetDisplay(wayland::display_t* display)
{
  if (display && !g_WlMessagePump)
  {
    // Start message processing as soon as we have a display
    g_WlMessagePump.reset(new CWinEventsWaylandThread(display));
  }
  else if (g_WlMessagePump)
  {
    // Stop if display is set to nullptr
    g_WlMessagePump->Stop();
    g_WlMessagePump.reset();
  }
}

bool CWinEventsWayland::MessagePump()
{
  // Forward any events that may have been pushed to our queue
  while (true)
  {
    XBMC_Event event;
    {
      // Scoped lock for reentrancy
      CSingleLock lock(m_queueMutex);

      if (m_queue.empty())
      {
        break;
      }

      // First get event and remove it from the queue, then pass it on - be aware that this
      // function must be reentrant
      event = m_queue.front();
      m_queue.pop();
    }

    g_application.OnEvent(event);
  }

  return true;
}

void CWinEventsWayland::MessagePush(XBMC_Event* ev)
{
  CSingleLock lock(m_queueMutex);
  m_queue.emplace(*ev);
}
