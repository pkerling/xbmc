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

#include <string>
#include <vector>

#include "storage/IStorageProvider.h"
#ifdef HAS_DBUS
#include "DBusUtil.h"

class CUDisks2Device
{
public:
  CUDisks2Device(const char *DeviceKitUDI);
  ~CUDisks2Device() { }

  void Update();

  bool Mount();
  bool UnMount();

  bool IsApproved();

  std::string toString();

  CMediaSource ToMediaShare();

  std::string m_UDI, m_DeviceKitUDI, m_MountPath, m_FileSystem, m_Label;
  bool m_isMounted, m_isMountedByUs, m_isRemovable, m_isPartition, m_isFileSystem, m_isSystemInternal, m_isOptical;
  int64_t m_PartitionSize;
};

class CUDisks2Provider : public IStorageProvider
{
public:
  CUDisks2Provider();
  virtual ~CUDisks2Provider();

  virtual void Initialize();
  virtual void Stop() { }

  virtual void GetLocalDrives(VECSOURCES &localDrives) { GetDisks(localDrives, false); }
  virtual void GetRemovableDrives(VECSOURCES &removableDrives) { GetDisks(removableDrives, true); }

  virtual bool Eject(const std::string& mountpath);

  virtual std::vector<std::string> GetDiskUsage();

  virtual bool PumpDriveChangeEvents(IStorageEventsCallback *callback);

  static bool HasUDisks();
private:
  typedef std::map<std::string, CUDisks2Device *> DeviceMap;
  typedef std::pair<std::string, CUDisks2Device *> DevicePair;

  void DeviceAdded(const char *object, IStorageEventsCallback *callback);
  void DeviceRemoved(const char *object, IStorageEventsCallback *callback);
  void DeviceChanged(const char *object, IStorageEventsCallback *callback);

  std::vector<std::string> EnumerateDisks();

  void GetDisks(VECSOURCES& devices, bool EnumerateRemovable);

  DeviceMap m_AvailableDevices;

  CDBusConnection m_connection;
};
#endif
