/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ClipboardImageTests.h"

#include "platform/ClipboardImage.h"

#include <QBuffer>
#include <QImage>
#include <QtEndian>

using namespace deskflow::clipboard;

namespace {

const QRgb kPixels[2][3] = {
    {qRgb(255, 0, 0), qRgb(0, 255, 0), qRgb(0, 0, 255)}, {qRgb(10, 20, 30), qRgb(255, 255, 255), qRgb(0, 0, 0)}
};

QImage expectedImage()
{
  QImage image(3, 2, QImage::Format_RGB888);
  for (int y = 0; y < 2; ++y)
    for (int x = 0; x < 3; ++x)
      image.setPixel(x, y, kPixels[y][x]);
  return image;
}

void append32(std::string &out, uint32_t v)
{
  for (int i = 0; i < 4; ++i)
    out.push_back(static_cast<char>((v >> (8 * i)) & 0xff));
}

void append16(std::string &out, uint16_t v)
{
  out.push_back(static_cast<char>(v & 0xff));
  out.push_back(static_cast<char>(v >> 8));
}

// A 32bpp bottom-up DIB the way Windows puts it on the clipboard: alpha byte 0.
std::string windowsDib32(bool bitfields)
{
  std::string dib;
  append32(dib, 40);
  append32(dib, 3);
  append32(dib, 2);
  append16(dib, 1);
  append16(dib, 32);
  append32(dib, bitfields ? 3 : 0);
  append32(dib, 3 * 2 * 4);
  for (int i = 0; i < 4; ++i)
    append32(dib, 0);
  if (bitfields) {
    append32(dib, 0x00ff0000);
    append32(dib, 0x0000ff00);
    append32(dib, 0x000000ff);
  }
  for (int y = 1; y >= 0; --y) {
    for (int x = 0; x < 3; ++x) {
      dib.push_back(static_cast<char>(qBlue(kPixels[y][x])));
      dib.push_back(static_cast<char>(qGreen(kPixels[y][x])));
      dib.push_back(static_cast<char>(qRed(kPixels[y][x])));
      dib.push_back(0);
    }
  }
  return dib;
}

// What Qt itself writes as a BMP, minus the 14-byte file header (24bpp).
std::string qtDib24()
{
  QByteArray bmp;
  QBuffer buffer(&bmp);
  buffer.open(QIODevice::WriteOnly);
  expectedImage().save(&buffer, "BMP");
  return bmp.mid(14).toStdString();
}

void verifyPng(const std::string &png)
{
  QVERIFY(!png.empty());
  QImage actual;
  QVERIFY(actual.loadFromData(QByteArray::fromStdString(png), "PNG"));
  QCOMPARE(actual.size(), QSize(3, 2));
  for (int y = 0; y < 2; ++y) {
    for (int x = 0; x < 3; ++x) {
      QCOMPARE(actual.pixel(x, y) & 0xffffff, kPixels[y][x] & 0xffffff);
      QCOMPARE(qAlpha(actual.pixel(x, y)), 255);
    }
  }
}

} // namespace

void ClipboardImageTests::roundTrip_24bit_preservesPixels()
{
  const auto png = dibToPng(qtDib24());
  verifyPng(png);
  verifyPng(dibToPng(pngToDib(png)));
}

void ClipboardImageTests::roundTrip_32bit_preservesPixels()
{
  const auto png = dibToPng(windowsDib32(false));
  verifyPng(png);
  verifyPng(dibToPng(pngToDib(png)));
}

void ClipboardImageTests::roundTrip_32bitBitfields_preservesPixels()
{
  verifyPng(dibToPng(windowsDib32(true)));
}

void ClipboardImageTests::dibToPng_garbage_returnsEmpty()
{
  QVERIFY(dibToPng("xyz").empty());
  QVERIFY(dibToPng(std::string(20, '\x7f')).empty());
}

void ClipboardImageTests::pngToDib_garbage_returnsEmpty()
{
  QVERIFY(pngToDib("").empty());
  QVERIFY(pngToDib("\x89PNG").empty());
}

void ClipboardImageTests::pngToDib_alphaPng_writesClassicHeader()
{
  // CF_DIB readers such as Paint expect a plain BITMAPINFOHEADER (biSize 40), not V4/V5.
  QImage image(2, 2, QImage::Format_ARGB32);
  image.fill(qRgba(10, 20, 30, 128));
  QByteArray png;
  QBuffer buffer(&png);
  buffer.open(QIODevice::WriteOnly);
  QVERIFY(image.save(&buffer, "PNG"));

  const auto dib = pngToDib(png.toStdString());
  QVERIFY(dib.size() > 4);
  QCOMPARE(qFromLittleEndian<quint32>(dib.data()), 40u);
}

QTEST_GUILESS_MAIN(ClipboardImageTests)
