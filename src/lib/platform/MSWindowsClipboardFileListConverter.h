/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "platform/MSWindowsClipboard.h"

//! Convert to/from CF_HDROP (files copied in Explorer)
class MSWindowsClipboardFileListConverter : public IMSWindowsClipboardConverter
{
public:
  IClipboard::Format getFormat() const override
  {
    return IClipboard::Format::FileList;
  }
  UINT getWin32Format() const override
  {
    return CF_HDROP;
  }
  HANDLE fromIClipboard(const std::string &) const override;
  std::string toIClipboard(HANDLE) const override;
};
