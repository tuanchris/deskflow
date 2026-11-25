/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2011 Nick Bolton
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "OSXClipboardTests.h"

#include "platform/OSXClipboard.h"
#include "platform/OSXClipboardUTF8Converter.h"

#include "deskflow/ClipboardFileList.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

using deskflow::clipboard::joinFileList;
using deskflow::clipboard::splitFileList;

void OSXClipboardTests::open()
{
  OSXClipboard clipboard;
  QVERIFY(clipboard.open(0));
  QVERIFY(clipboard.empty());
  clipboard.close();
}

void OSXClipboardTests::singleFormat()
{
  using enum IClipboard::Format;

  OSXClipboard clipboard;
  QVERIFY(clipboard.empty());
  clipboard.add(Text, m_testString);
  QVERIFY(clipboard.has(Text));
  QCOMPARE(clipboard.get(Text), m_testString);
}

void OSXClipboardTests::formatConvert_UTF8()
{
  OSXClipboardUTF8Converter converter;
  QCOMPARE(IClipboard::Format::Text, converter.getFormat());
  QCOMPARE(converter.getOSXFormat(), CFSTR("public.utf8-plain-text"));
  QCOMPARE(converter.fromIClipboard("test data\n"), "test data\r");
  QCOMPARE(converter.toIClipboard("test data\r"), "test data\n");
}

void OSXClipboardTests::fileList_addThenGet_roundTrips()
{
  using enum IClipboard::Format;

  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const auto base = dir.path();
  for (const auto &name : {QStringLiteral("a file.txt"), QString::fromUtf8("r\xc3\xa9sum\xc3\xa9.pdf")}) {
    QFile file(base + QLatin1Char('/') + name);
    QVERIFY(file.open(QIODevice::WriteOnly));
  }
  QVERIFY(QDir(base).mkdir(QStringLiteral("folder")));
  const std::vector<std::string> paths = {
      (base + QStringLiteral("/a file.txt")).toStdString(),
      (base + QString::fromUtf8("/r\xc3\xa9sum\xc3\xa9.pdf")).toStdString(),
      (base + QStringLiteral("/folder/")).toStdString(),
  };

  OSXClipboard clipboard;
  QVERIFY(clipboard.open(0));
  QVERIFY(clipboard.empty());
  clipboard.add(FileList, joinFileList(paths));
  clipboard.close();

  QVERIFY(clipboard.open(0));
  QVERIFY(clipboard.has(FileList));
  QCOMPARE(splitFileList(clipboard.get(FileList)), paths);
  clipboard.close();
}

void OSXClipboardTests::png_addThenGet_passthrough()
{
  using enum IClipboard::Format;
  const std::string png = std::string("\x89PNG\r\n\x1a\n", 8) + "abcd";

  OSXClipboard clipboard;
  QVERIFY(clipboard.empty());
  clipboard.add(PNG, png);
  QVERIFY(clipboard.has(PNG));
  QCOMPARE(clipboard.get(PNG), png);
  QVERIFY(!clipboard.has(Bitmap));
}

void OSXClipboardTests::text_unaffectedByFileList()
{
  using enum IClipboard::Format;

  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const std::vector<std::string> paths = {dir.path().toStdString()};

  OSXClipboard clipboard;
  QVERIFY(clipboard.empty());
  clipboard.add(Text, m_testString);
  clipboard.add(FileList, joinFileList(paths));
  QVERIFY(clipboard.has(Text));
  QCOMPARE(clipboard.get(Text), m_testString);
  QCOMPARE(splitFileList(clipboard.get(FileList)), paths);
}

void OSXClipboardTests::fileList_folderMarker_withoutStat()
{
  using enum IClipboard::Format;

  // Paths that do not exist here (an unmounted share): the folder marker alone decides.
  const std::vector<std::string> paths = {"/Volumes/not-mounted/dir/", "/Volumes/not-mounted/file.txt"};

  OSXClipboard clipboard;
  QVERIFY(clipboard.empty());
  clipboard.add(FileList, joinFileList(paths));
  QCOMPARE(splitFileList(clipboard.get(FileList)), paths);
}

QTEST_MAIN(OSXClipboardTests)
