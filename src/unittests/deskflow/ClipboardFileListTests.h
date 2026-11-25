/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "base/Log.h"

#include <QTest>

class ClipboardFileListTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void initTestCase();
  void split_join_roundTrip();
  void map_macToWindows_uncAndBackslashes();
  void map_prefixBoundary_noPartialMatch();
  void map_exactFolder_matches();
  void map_longestPrefixWins();
  void map_windowsToMac_caseInsensitive();
  void map_trailingSlashInRule_ignored();
  void map_badRule_skipped();
  void map_folderMarker_preserved();
  void applyFileMappings_noMatch_keepsOtherFormats();
  void applyFileMappings_rewritesPayload();
  void fileMappingRules_readsDocumentedIniList();

private:
  Log m_log;
};
