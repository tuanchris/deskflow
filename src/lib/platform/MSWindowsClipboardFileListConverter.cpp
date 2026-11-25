/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/MSWindowsClipboardFileListConverter.h"

#include "deskflow/ClipboardFileList.h"

#include <QString>

#include <shellapi.h>
#include <shlobj.h>

#include <cstring>

HANDLE MSWindowsClipboardFileListConverter::fromIClipboard(const std::string &payload) const
{
  const auto paths = deskflow::clipboard::splitFileList(payload);
  if (paths.empty())
    return nullptr;

  // DROPFILES header, then each path NUL-terminated, then a final NUL.
  std::wstring list;
  for (const auto &path : paths) {
    list += QString::fromStdString(path).toStdWString();
    list.push_back(L'\0');
  }
  list.push_back(L'\0');

  const auto listBytes = list.size() * sizeof(wchar_t);
  HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DROPFILES) + listBytes);
  if (handle == nullptr)
    return nullptr;

  auto *drop = static_cast<DROPFILES *>(GlobalLock(handle));
  if (drop == nullptr) {
    GlobalFree(handle);
    return nullptr;
  }
  drop->pFiles = sizeof(DROPFILES);
  drop->fWide = TRUE;
  std::memcpy(reinterpret_cast<char *>(drop) + sizeof(DROPFILES), list.data(), listBytes);
  GlobalUnlock(handle);
  return handle;
}

std::string MSWindowsClipboardFileListConverter::toIClipboard(HANDLE data) const
{
  auto drop = static_cast<HDROP>(data);
  const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);

  std::vector<std::string> paths;
  for (UINT i = 0; i < count; ++i) {
    const UINT length = DragQueryFileW(drop, i, nullptr, 0);
    std::wstring path(length + 1, L'\0');
    DragQueryFileW(drop, i, path.data(), length + 1);
    path.resize(length);
    paths.push_back(QString::fromStdWString(path).toStdString());
  }
  return deskflow::clipboard::joinFileList(paths);
}
