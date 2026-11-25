/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include <QTest>

class ClipboardImageTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void roundTrip_24bit_preservesPixels();
  void roundTrip_32bit_preservesPixels();
  void roundTrip_32bitBitfields_preservesPixels();
  void dibToPng_garbage_returnsEmpty();
  void pngToDib_garbage_returnsEmpty();
  void pngToDib_alphaPng_writesClassicHeader();
};
