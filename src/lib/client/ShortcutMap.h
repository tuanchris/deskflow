/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "deskflow/KeyTypes.h"

#include <QStringList>

#include <optional>
#include <utility>
#include <vector>

struct Shortcut
{
  KeyID id;
  KeyModifierMask mask;
};

//! Per-machine shortcut translation (core/shortcutMappings), e.g. "super+q|alt+F4".
//! The left side is matched against keys as the server sends them, before any modifier
//! remap; the right side is what this machine synthesizes instead.
class ShortcutMap
{
public:
  explicit ShortcutMap(const QStringList &rules = {});

  std::optional<Shortcut> match(KeyID id, KeyModifierMask mask) const;

private:
  std::vector<std::pair<Shortcut, Shortcut>> m_rules;
};
