/*
 *      Copyright (C) 2013 Arne Morten Kvarving
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
#include "ImageDecoder.h"
#include "kodi-addon-dev-kit/include/kodi/kodi_imagedec_types.h"
#include "guilib/TextureFormats.h"

static const std::map<int,int> KodiToAddonFormat = {{XB_FMT_A8R8G8B8, ADDON_IMG_FMT_A8R8G8B8},
                                                    {XB_FMT_A8,       ADDON_IMG_FMT_A8},
                                                    {XB_FMT_RGBA8,    ADDON_IMG_FMT_RGBA8},
                                                    {XB_FMT_RGB8,     ADDON_IMG_FMT_RGB8}};

namespace ADDON
{

std::unique_ptr<CImageDecoder>
CImageDecoder::FromExtension(CAddonInfo&& addonInfo, const cp_extension_t* ext)
{
  std::string mime = CAddonMgr::GetInstance().GetExtValue(ext->configuration, "@mimetype");
  std::string extension = CAddonMgr::GetInstance().GetExtValue(ext->configuration, "@extension");
  return std::unique_ptr<CImageDecoder>(new CImageDecoder(std::move(addonInfo),
                                                          std::move(mime),
                                                          std::move(extension)));
}

CImageDecoder::CImageDecoder(CAddonInfo&& addonInfo, std::string mime, std::string extension) :
  CAddonDll(std::move(addonInfo)),
  m_mimetype(std::move(mime)),
  m_extension(std::move(extension))
{
}

CImageDecoder::~CImageDecoder()
{
  if (m_image && Initialized())
    m_struct.toAddon.Close(m_image);
}

bool CImageDecoder::LoadImageFromMemory(unsigned char* buffer, unsigned int bufSize,
                                        unsigned int width, unsigned int height)
{
  if (!Initialized())
    return false;

  m_width = width;
  m_height = height;
  m_image = m_struct.toAddon.LoadImage(buffer, bufSize, &m_width, &m_height);

  return m_image != nullptr;
}

bool CImageDecoder::Decode(unsigned char* const pixels, unsigned int width,
                           unsigned int height, unsigned int pitch,
                           unsigned int format)
{
  if (!Initialized())
    return false;

  auto it = KodiToAddonFormat.find(format & XB_FMT_MASK);
  if (it == KodiToAddonFormat.end())
    return false;

  bool result = m_struct.toAddon.Decode(m_image, pixels, width, height, pitch, it->second);
  m_width = width;
  m_height = height;

  return result;
}

bool CImageDecoder::Create(const std::string& mimetype)
{
  m_struct.props.mimetype = mimetype.c_str();
  m_struct.toKodi.kodiInstance = this;
  return CAddonDll::Create(ADDON_INSTANCE_IMAGEDECODER, &m_struct, &m_struct.props) == ADDON_STATUS_OK;
}

} /*namespace ADDON*/
