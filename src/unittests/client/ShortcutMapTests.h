/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "base/Log.h"

#include <QTest>

class ShortcutMapTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void match_superQ_returnsAltF4();
  void match_ignoresLockBits();
  void match_shiftedLetter_caseInsensitive();
  void match_extraModifier_noHit();
  void match_namedKeys();
  void badRule_skipped();

private:
  Log m_log;
};
