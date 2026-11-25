/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "deskflow/ClipboardFileList.h"

#include "base/Log.h"
#include "common/Settings.h"
#include "deskflow/Clipboard.h"

#include <array>

namespace deskflow::clipboard {

namespace {

struct Rule
{
  QString from;
  QString to;
  Qt::CaseSensitivity cs;
};

// Forward slashes only, no trailing separator.
QString normalise(QString path)
{
  path.replace(QLatin1Char('\\'), QLatin1Char('/'));
  while (path.size() > 1 && path.endsWith(QLatin1Char('/')))
    path.chop(1);
  return path;
}

// "C:/..." or "//host/share": Windows paths compare case-insensitively.
bool isWindowsPath(const QString &path)
{
  return (path.size() >= 2 && path[0].isLetter() && path[1] == QLatin1Char(':')) || path.startsWith(QStringLiteral("//"));
}

std::vector<Rule> parseRules(const QStringList &rules)
{
  std::vector<Rule> parsed;
  for (const auto &rule : rules) {
    const auto separator = rule.indexOf(QLatin1Char('|'));
    const auto from = normalise(rule.left(separator).trimmed());
    const auto to = normalise(rule.mid(separator + 1).trimmed());
    if (separator < 0 || from.isEmpty() || to.isEmpty()) {
      LOG_WARN("ignoring clipboard file mapping, expected source|target: %s", qPrintable(rule));
      continue;
    }
    parsed.push_back({from, to, isWindowsPath(from) ? Qt::CaseInsensitive : Qt::CaseSensitive});
  }
  return parsed;
}

} // namespace

std::vector<std::string>
mapFilePaths(const std::vector<std::string> &paths, const QStringList &rules, bool targetIsWindows)
{
  const auto parsed = parseRules(rules);
  std::vector<std::string> mapped;

  for (const auto &path : paths) {
    const auto source = normalise(QString::fromStdString(path));

    const Rule *best = nullptr;
    for (const auto &rule : parsed) {
      const bool matches = source.compare(rule.from, rule.cs) == 0 ||
                           source.startsWith(rule.from + QLatin1Char('/'), rule.cs);
      if (matches && (best == nullptr || rule.from.size() > best->from.size()))
        best = &rule;
    }

    if (best == nullptr) {
      LOG_WARN("clipboard file not under any mapping: %s", path.c_str());
      continue;
    }

    auto target = best->to + source.mid(best->from.size());
    if (targetIsWindows)
      target.replace(QLatin1Char('/'), QLatin1Char('\\'));
    mapped.push_back(target.toStdString());
  }

  return mapped;
}

void applyFileMappings(Clipboard &clipboard, const QStringList &rules, bool targetIsWindows)
{
  constexpr auto kFormats = static_cast<int>(IClipboard::Format::TotalFormats);
  constexpr auto kFileList = static_cast<int>(IClipboard::Format::FileList);

  if (!clipboard.open(clipboard.getTime()))
    return;

  if (!clipboard.has(IClipboard::Format::FileList)) {
    clipboard.close();
    return;
  }

  // Clipboard has no per-format remove, so snapshot, empty and re-add.
  std::array<bool, kFormats> present{};
  std::array<std::string, kFormats> data;
  for (int i = 0; i < kFormats; ++i) {
    const auto format = static_cast<IClipboard::Format>(i);
    present[i] = clipboard.has(format);
    if (present[i])
      data[i] = clipboard.get(format);
  }

  const auto mapped = mapFilePaths(splitFileList(data[kFileList]), rules, targetIsWindows);

  clipboard.empty();
  for (int i = 0; i < kFormats; ++i) {
    if (present[i] && i != kFileList)
      clipboard.add(static_cast<IClipboard::Format>(i), data[i]);
  }
  if (!mapped.empty())
    clipboard.add(IClipboard::Format::FileList, joinFileList(mapped));

  clipboard.close();
}

QStringList fileMappingRules()
{
  return Settings::value(Settings::Core::ClipboardFileMappings).toStringList();
}

} // namespace deskflow::clipboard
