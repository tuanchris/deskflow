/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "platform/ClipboardImage.h"
#include "platform/MSWindowsClipboard.h"
#include "platform/MSWindowsClipboardBitmapConverter.h"

#include <cstring>

//! The registered "PNG" clipboard format (Chrome, Office, Slack, Snipping Tool): passed through.
class MSWindowsClipboardPNGConverter : public IMSWindowsClipboardConverter
{
public:
  IClipboard::Format getFormat() const override
  {
    return IClipboard::Format::PNG;
  }
  UINT getWin32Format() const override
  {
    return m_format;
  }
  HANDLE fromIClipboard(const std::string &png) const override
  {
    HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE, png.size());
    if (handle == nullptr)
      return nullptr;
    void *dst = GlobalLock(handle);
    if (dst == nullptr) {
      GlobalFree(handle);
      return nullptr;
    }
    std::memcpy(dst, png.data(), png.size());
    GlobalUnlock(handle);
    return handle;
  }
  std::string toIClipboard(HANDLE data) const override
  {
    const void *src = GlobalLock(data);
    if (src == nullptr)
      return {};
    std::string png(static_cast<const char *>(src), GlobalSize(data));
    GlobalUnlock(data);
    return png;
  }

private:
  UINT m_format = RegisterClipboardFormatW(L"PNG");
};

//! CF_DIB, transcoded to and from PNG, for apps that only speak DIB (Paint, older apps).
class MSWindowsClipboardDIBPNGConverter : public IMSWindowsClipboardConverter
{
public:
  IClipboard::Format getFormat() const override
  {
    return IClipboard::Format::PNG;
  }
  UINT getWin32Format() const override
  {
    return CF_DIB;
  }
  HANDLE fromIClipboard(const std::string &png) const override
  {
    const auto dib = deskflow::clipboard::pngToDib(png);
    return dib.empty() ? nullptr : m_bitmap.fromIClipboard(dib);
  }
  std::string toIClipboard(HANDLE data) const override
  {
    return deskflow::clipboard::dibToPng(m_bitmap.toIClipboard(data));
  }

private:
  MSWindowsClipboardBitmapConverter m_bitmap;
};
