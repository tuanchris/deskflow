/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "client/ShortcutMap.h"

#include "base/Log.h"
#include "deskflow/KeyMap.h"

namespace {

constexpr KeyModifierMask kRelevantModifiers =
    KeyModifierShift | KeyModifierControl | KeyModifierAlt | KeyModifierAltGr | KeyModifierMeta | KeyModifierSuper;

// With Shift held the server sends 'Z', not 'z'; rules name the letter either way.
KeyID foldCase(KeyID id)
{
  return (id >= 'A' && id <= 'Z') ? id - 'A' + 'a' : id;
}

std::optional<Shortcut> parse(std::string text)
{
  Shortcut shortcut{kKeyNone, 0};
  if (!deskflow::KeyMap::parseModifiers(text, shortcut.mask) || !deskflow::KeyMap::parseKey(text, shortcut.id) ||
      shortcut.id == kKeyNone) {
    return std::nullopt;
  }
  return shortcut;
}

} // namespace

ShortcutMap::ShortcutMap(const QStringList &rules)
{
  for (const auto &rule : rules) {
    const auto separator = rule.indexOf(QLatin1Char('|'));
    const auto from = parse(rule.left(separator).trimmed().toStdString());
    const auto to = parse(rule.mid(separator + 1).trimmed().toStdString());
    if (separator < 0 || !from || !to) {
      LOG_WARN("ignoring shortcut mapping, expected from|to such as super+q|alt+F4: %s", qPrintable(rule));
      continue;
    }
    m_rules.emplace_back(Shortcut{foldCase(from->id), from->mask & kRelevantModifiers}, *to);
  }
}

std::optional<Shortcut> ShortcutMap::match(KeyID id, KeyModifierMask mask) const
{
  const auto key = foldCase(id);
  const auto modifiers = mask & kRelevantModifiers;
  for (const auto &[from, to] : m_rules) {
    if (from.id == key && from.mask == modifiers)
      return to;
  }
  return std::nullopt;
}
