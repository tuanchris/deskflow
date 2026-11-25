/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QStringList>

#include <string>
#include <string_view>
#include <vector>

class Clipboard;

//! Helpers for IClipboard::Format::FileList: NUL-separated absolute paths; folders end in a separator.
//!
//! Files are not transferred. The receiving machine rewrites each path to where the
//! source folder is reachable over SMB (core/clipboardFileMappings), and the OS file
//! manager performs the copy on paste.
namespace deskflow::clipboard {

#ifdef _WIN32
inline constexpr bool kTargetIsWindows = true;
#else
inline constexpr bool kTargetIsWindows = false;
#endif

// Header-only so platform converters can use these without linking the app lib.
inline std::vector<std::string> splitFileList(std::string_view payload)
{
  std::vector<std::string> paths;
  size_t start = 0;
  while (start <= payload.size()) {
    const auto found = payload.find('\0', start);
    const auto end = found == std::string_view::npos ? payload.size() : found;
    if (end > start)
      paths.emplace_back(payload.substr(start, end - start));
    start = end + 1;
  }
  return paths;
}

//! A trailing separator marks a folder, so a receiver never has to stat a path over SMB.
inline bool isFolder(std::string_view path)
{
  return !path.empty() && (path.back() == '/' || path.back() == '\\');
}

inline std::string joinFileList(const std::vector<std::string> &paths)
{
  std::string payload;
  for (const auto &path : paths) {
    if (!payload.empty())
      payload.push_back('\0');
    payload += path;
  }
  return payload;
}

//! Rewrites paths using "sourcePrefix|targetPrefix" rules; the longest matching prefix
//! wins. Rules may use / or \. Paths no rule matches are dropped (and logged).
std::vector<std::string>
mapFilePaths(const std::vector<std::string> &paths, const QStringList &rules, bool targetIsWindows);

//! Rewrites the FileList format in place. Removes it if no path maps; other formats are kept.
void applyFileMappings(Clipboard &clipboard, const QStringList &rules, bool targetIsWindows);

//! The rules from the core/clipboardFileMappings setting.
QStringList fileMappingRules();

} // namespace deskflow::clipboard
