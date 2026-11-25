/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ShortcutMapTests.h"

#include "client/ShortcutMap.h"

void ShortcutMapTests::match_superQ_returnsAltF4()
{
  const ShortcutMap map({"super+q|alt+F4"});
  const auto hit = map.match('q', KeyModifierSuper);
  QVERIFY(hit.has_value());
  QCOMPARE(hit->id, kKeyF4);
  QCOMPARE(hit->mask, KeyModifierAlt);
}

void ShortcutMapTests::match_ignoresLockBits()
{
  const ShortcutMap map({"super+q|alt+F4"});
  QVERIFY(map.match('q', KeyModifierSuper | KeyModifierCapsLock | KeyModifierNumLock).has_value());
}

void ShortcutMapTests::match_shiftedLetter_caseInsensitive()
{
  const ShortcutMap map({"super+shift+z|control+y"});
  const auto hit = map.match('Z', KeyModifierSuper | KeyModifierShift);
  QVERIFY(hit.has_value());
  QCOMPARE(hit->id, KeyID('y'));
  QCOMPARE(hit->mask, KeyModifierControl);
}

void ShortcutMapTests::match_extraModifier_noHit()
{
  const ShortcutMap map({"super+q|alt+F4"});
  QVERIFY(!map.match('q', KeyModifierSuper | KeyModifierShift).has_value());
  QVERIFY(!map.match('q', 0).has_value());
}

void ShortcutMapTests::match_namedKeys()
{
  const ShortcutMap map({"super+Left|Home", "alt+BackSpace|control+BackSpace"});

  const auto home = map.match(kKeyLeft, KeyModifierSuper);
  QVERIFY(home.has_value());
  QCOMPARE(home->id, kKeyHome);
  QCOMPARE(home->mask, KeyModifierMask(0));

  const auto word = map.match(kKeyBackSpace, KeyModifierAlt);
  QVERIFY(word.has_value());
  QCOMPARE(word->id, kKeyBackSpace);
  QCOMPARE(word->mask, KeyModifierControl);
}

void ShortcutMapTests::badRule_skipped()
{
  const ShortcutMap map({"garbage", "super+q|", "|alt+F4", "super|alt+F4", "nosuchmod+q|alt+F4"});
  QVERIFY(!map.match('q', KeyModifierSuper).has_value());
  QVERIFY(!map.match(kKeyNone, KeyModifierSuper).has_value());
}

QTEST_MAIN(ShortcutMapTests)
