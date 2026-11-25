/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

//! Image conversions for IClipboard::Format::PNG.
//! A "DIB" here is a BMP without its 14-byte file header (the CF_DIB layout).
namespace deskflow::clipboard {

//! Offset of the pixel data from the start of a DIB, or 0 if the header is unreadable.
uint32_t dibPixelOffset(const uint8_t *dib, size_t dibSize);

//! Returns PNG bytes, or empty if the DIB cannot be decoded.
std::string dibToPng(std::string_view dib);

//! Returns a DIB, or empty if the PNG cannot be decoded.
std::string pngToDib(std::string_view png);

} // namespace deskflow::clipboard
