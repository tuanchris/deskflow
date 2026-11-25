/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "platform/OSXClipboard.h"

//! public.png passes through unchanged: it is already the wire format.
class OSXClipboardPNGConverter : public IOSXClipboardConverter
{
public:
  IClipboard::Format getFormat() const override
  {
    return IClipboard::Format::PNG;
  }
  CFStringRef getOSXFormat() const override
  {
    return CFSTR("public.png");
  }
  std::string fromIClipboard(const std::string &data) const override
  {
    return data;
  }
  std::string toIClipboard(const std::string &data) const override
  {
    return data;
  }
};
