/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ClipboardFileListTests.h"

#include "common/Settings.h"
#include "deskflow/Clipboard.h"

#include <QFile>
#include <QTemporaryDir>

// Regression guard: <Windows.h> without NOMINMAX defines min/max as macros after Qt has
// undefined them, and Windows converters include this header in that state.
#define min(a, b) (((a) < (b)) ? (a) : (b))
#include "deskflow/ClipboardFileList.h"
#undef min

using namespace deskflow::clipboard;
using Paths = std::vector<std::string>;

void ClipboardFileListTests::initTestCase()
{
  m_log.setFilter(LogLevel::Level::Verbose);
}

void ClipboardFileListTests::split_join_roundTrip()
{
  const Paths paths = {"/a b", "/é/照片"};
  QCOMPARE(splitFileList(joinFileList(paths)), paths);
  QVERIFY(splitFileList("").empty());
}

void ClipboardFileListTests::map_macToWindows_uncAndBackslashes()
{
  const auto result = mapFilePaths({"/Users/me/Docs/a b.pdf"}, {"/Users/me|//mac.local/me"}, true);
  QCOMPARE(result, Paths{"\\\\mac.local\\me\\Docs\\a b.pdf"});
}

void ClipboardFileListTests::map_prefixBoundary_noPartialMatch()
{
  QVERIFY(mapFilePaths({"/Users/mex/a"}, {"/Users/me|//m/t"}, true).empty());
}

void ClipboardFileListTests::map_exactFolder_matches()
{
  QCOMPARE(mapFilePaths({"/Users/me"}, {"/Users/me|//m/t"}, true), Paths{"\\\\m\\t"});
}

void ClipboardFileListTests::map_longestPrefixWins()
{
  const QStringList rules = {"/Volumes|//m/v", "/Volumes/SSD|//m/ssd"};
  QCOMPARE(mapFilePaths({"/Volumes/SSD/x"}, rules, true), Paths{"\\\\m\\ssd\\x"});
}

void ClipboardFileListTests::map_windowsToMac_caseInsensitive()
{
  const auto result = mapFilePaths({"c:\\users\\ME\\Desktop\\x.png"}, {"C:/Users/me|/Volumes/me"}, false);
  QCOMPARE(result, Paths{"/Volumes/me/Desktop/x.png"});
}

void ClipboardFileListTests::map_trailingSlashInRule_ignored()
{
  QCOMPARE(mapFilePaths({"/Users/me/a"}, {"/Users/me/|//m/t/"}, true), Paths{"\\\\m\\t\\a"});
}

void ClipboardFileListTests::map_badRule_skipped()
{
  QVERIFY(mapFilePaths({"/Users/me/a"}, {"no-separator", "|", "/Users/me|"}, true).empty());
}

void ClipboardFileListTests::map_folderMarker_preserved()
{
  // A trailing separator marks a folder so the receiver never has to stat it over SMB.
  QCOMPARE(mapFilePaths({"/Users/me/folder/"}, {"/Users/me|//m/t"}, true), Paths{"\\\\m\\t\\folder\\"});
  QCOMPARE(mapFilePaths({"C:\\Users\\me\\dir\\"}, {"C:/Users/me|/Volumes/me"}, false), Paths{"/Volumes/me/dir/"});
  QCOMPARE(mapFilePaths({"/Users/me/file"}, {"/Users/me|//m/t"}, true), Paths{"\\\\m\\t\\file"});
}

void ClipboardFileListTests::applyFileMappings_noMatch_keepsOtherFormats()
{
  Clipboard clipboard;
  clipboard.open(0);
  clipboard.empty();
  clipboard.add(IClipboard::Format::Text, "a file.txt");
  clipboard.add(IClipboard::Format::FileList, joinFileList({"/elsewhere/a file.txt"}));
  clipboard.close();

  applyFileMappings(clipboard, {"/Users/me|//m/t"}, true);

  clipboard.open(0);
  QVERIFY(!clipboard.has(IClipboard::Format::FileList));
  QCOMPARE(clipboard.get(IClipboard::Format::Text), std::string("a file.txt"));
  clipboard.close();
}

void ClipboardFileListTests::applyFileMappings_rewritesPayload()
{
  Clipboard clipboard;
  clipboard.open(0);
  clipboard.empty();
  clipboard.add(IClipboard::Format::FileList, joinFileList({"/Users/me/a"}));
  clipboard.close();

  applyFileMappings(clipboard, {"/Users/me|//m/t"}, true);

  clipboard.open(0);
  QCOMPARE(clipboard.get(IClipboard::Format::FileList), joinFileList({"\\\\m\\t\\a"}));
  clipboard.close();
}

void ClipboardFileListTests::fileMappingRules_readsDocumentedIniList()
{
  // The exact line docs/Configuration.md tells users to write.
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const auto path = dir.filePath(QStringLiteral("Deskflow.conf"));
  QFile file(path);
  QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
  file.write("[core]\nclipboardFileMappings=/Users/me|//my-mac.local/me, /Volumes/SSD|//my-mac.local/SSD\n");
  file.close();

  Settings::setSettingsFile(path);
  QCOMPARE(fileMappingRules(), QStringList({"/Users/me|//my-mac.local/me", "/Volumes/SSD|//my-mac.local/SSD"}));
}

QTEST_MAIN(ClipboardFileListTests)
