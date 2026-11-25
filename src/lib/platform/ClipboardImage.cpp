/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/ClipboardImage.h"

#include <QBuffer>
#include <QImage>
#include <QtEndian>

namespace deskflow::clipboard {

namespace {

constexpr uint32_t kBiBitfields = 3;
constexpr uint32_t kBiAlphabitfields = 6;
constexpr uint32_t kBmpFileHeaderSize = 14;
constexpr uint32_t kInfoHeaderSize = 40;

// Qt maps PNG quality 80 to zlib level 1: several times faster than the default level and
// only slightly larger, which matters because conversion runs on the input thread.
constexpr int kFastPngQuality = 80;

std::string encode(const QImage &image, const char *format, int quality = -1)
{
  QByteArray bytes;
  QBuffer buffer(&bytes);
  buffer.open(QIODevice::WriteOnly);
  if (image.isNull() || !image.save(&buffer, format, quality))
    return {};
  return bytes.toStdString();
}

} // namespace

uint32_t dibPixelOffset(const uint8_t *dib, size_t dibSize)
{
  uint32_t pixelOffset = 0;
  if (dibSize >= 16) {
    const auto biSize = qFromLittleEndian<quint32>(dib);
    if (biSize >= 12 && biSize <= dibSize) {
      pixelOffset = biSize;
      if (biSize >= kInfoHeaderSize) {
        const auto biBitCount = qFromLittleEndian<quint16>(dib + 14);
        const auto biCompression = qFromLittleEndian<quint32>(dib + 16);
        if (biSize == kInfoHeaderSize) {
          if (biCompression == kBiBitfields) {
            pixelOffset += 12;
          } else if (biCompression == kBiAlphabitfields) {
            pixelOffset += 16;
          }
        }
        if (biBitCount > 0 && biBitCount <= 8) {
          const auto biClrUsed = qFromLittleEndian<quint32>(dib + 32);
          const auto numColors = biClrUsed != 0 ? biClrUsed : (1u << biBitCount);
          pixelOffset += numColors * 4;
        }
      }
    }
  }
  return pixelOffset;
}

std::string dibToPng(std::string_view dib)
{
  const auto *raw = reinterpret_cast<const uint8_t *>(dib.data());
  const auto pixelOffset = dibPixelOffset(raw, dib.size());
  if (pixelOffset == 0 || pixelOffset >= dib.size())
    return {};

  QByteArray bmp(kBmpFileHeaderSize, '\0');
  bmp[0] = 'B';
  bmp[1] = 'M';
  qToLittleEndian<quint32>(static_cast<quint32>(kBmpFileHeaderSize + dib.size()), bmp.data() + 2);
  qToLittleEndian<quint32>(kBmpFileHeaderSize + pixelOffset, bmp.data() + 10);
  bmp.append(dib.data(), static_cast<qsizetype>(dib.size()));

  QImage image;
  if (!image.loadFromData(bmp, "BMP"))
    return {};

  return encode(image, "PNG", kFastPngQuality);
}

std::string pngToDib(std::string_view png)
{
  QImage image;
  if (!image.loadFromData(QByteArray(png.data(), static_cast<qsizetype>(png.size())), "PNG"))
    return {};

  auto bmp = encode(image, "BMP");
  if (bmp.size() <= kBmpFileHeaderSize)
    return {};
  return bmp.substr(kBmpFileHeaderSize);
}

} // namespace deskflow::clipboard
