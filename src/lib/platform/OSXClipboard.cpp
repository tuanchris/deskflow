/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2004 Chris Schoeneman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/OSXClipboard.h"

#include "arch/ArchException.h"
#include "base/Log.h"
#include "deskflow/ClipboardFileList.h"
#include "platform/OSXClipboardHTMLConverter.h"
#include "platform/OSXClipboardPNGConverter.h"
#include "platform/OSXClipboardTextConverter.h"
#include "platform/OSXClipboardUTF16Converter.h"
#include "platform/OSXClipboardUTF8Converter.h"

#include <cstring>

//
// OSXClipboard
//

OSXClipboard::OSXClipboard() : m_time(0), m_pboard(nullptr)
{
  m_converters.push_back(new OSXClipboardHTMLConverter);
  m_converters.push_back(new OSXClipboardPNGConverter);
  m_converters.push_back(new OSXClipboardUTF8Converter);
  m_converters.push_back(new OSXClipboardUTF16Converter);
  m_converters.push_back(new OSXClipboardTextConverter);

  OSStatus createErr = PasteboardCreate(kPasteboardClipboard, &m_pboard);
  if (createErr != noErr) {
    LOG_WARN("failed to create clipboard reference: error %i", createErr);
    LOG_ERR("unable to connect to pasteboard, clipboard sharing disabled", createErr);
    m_pboard = nullptr;
    return;
  }

  OSStatus syncErr = PasteboardSynchronize(m_pboard);
  if (syncErr != noErr) {
    LOG_WARN("failed to syncronize clipboard: error %i", syncErr);
  }
}

OSXClipboard::~OSXClipboard()
{
  clearConverters();
}

bool OSXClipboard::empty()
{
  LOG_DEBUG("emptying clipboard");
  if (m_pboard == nullptr)
    return false;

  OSStatus err = PasteboardClear(m_pboard);
  if (err != noErr) {
    LOG_WARN("failed to clear clipboard: error %i", err);
    return false;
  }

  return true;
}

bool OSXClipboard::synchronize()
{
  if (m_pboard == nullptr)
    return false;

  PasteboardSyncFlags flags = PasteboardSynchronize(m_pboard);
  LOG_VERBOSE("flags: %x", flags);

  if (flags & kPasteboardModified) {
    return true;
  }
  return false;
}

void OSXClipboard::add(Format format, const std::string &data)
{
  if (m_pboard == nullptr)
    return;

  LOG_DEBUG("add %d bytes to clipboard format: %d", data.size(), format);
  if (format == IClipboard::Format::FileList) {
    addFileList(data);
    PasteboardSynchronize(m_pboard);
    return;
  }
  if (format == IClipboard::Format::Text) {
    LOG_DEBUG("format of data to be added to clipboard was kText");
  } else if (format == IClipboard::Format::Bitmap) {
    LOG_DEBUG("format of data to be added to clipboard was kBitmap");
  } else if (format == IClipboard::Format::HTML) {
    LOG_DEBUG("format of data to be added to clipboard was kHTML");
  }

  for (ConverterList::const_iterator index = m_converters.begin(); index != m_converters.end(); ++index) {

    IOSXClipboardConverter *converter = *index;

    // skip converters for other formats
    if (converter->getFormat() == format) {
      std::string osXData = converter->fromIClipboard(data);
      CFStringRef flavorType = converter->getOSXFormat();
      CFDataRef dataRef = CFDataCreate(kCFAllocatorDefault, (uint8_t *)osXData.data(), osXData.size());
      PasteboardItemID itemID = 0;

      if (dataRef) {
        PasteboardPutItemFlavor(m_pboard, itemID, flavorType, dataRef, kPasteboardFlavorNoFlags);

        CFRelease(dataRef);
        LOG_DEBUG("added %d bytes to clipboard format: %d", data.size(), format);
      }
    }
  }
  PasteboardSynchronize(m_pboard);
}

bool OSXClipboard::open(Time time) const
{
  if (m_pboard == nullptr)
    return false;

  LOG_DEBUG("opening clipboard");
  m_time = time;
  return true;
}

void OSXClipboard::close() const
{
  LOG_DEBUG("closing clipboard");
  /* not needed */
}

IClipboard::Time OSXClipboard::getTime() const
{
  return m_time;
}

bool OSXClipboard::has(Format format) const
{
  if (m_pboard == nullptr)
    return false;

  if (format == Format::FileList)
    return hasFileList();

  PasteboardItemID item;
  PasteboardGetItemIdentifier(m_pboard, (CFIndex)1, &item);

  for (ConverterList::const_iterator index = m_converters.begin(); index != m_converters.end(); ++index) {
    IOSXClipboardConverter *converter = *index;
    if (converter->getFormat() == format) {
      PasteboardFlavorFlags flags;
      CFStringRef type = converter->getOSXFormat();

      OSStatus res;

      if ((res = PasteboardGetItemFlavorFlags(m_pboard, item, type, &flags)) == noErr) {
        return true;
      }
    }
  }

  return false;
}

std::string OSXClipboard::get(Format format) const
{
  CFStringRef type;
  PasteboardItemID item;
  std::string result;

  if (m_pboard == nullptr)
    return result;

  if (format == Format::FileList)
    return getFileList();

  PasteboardGetItemIdentifier(m_pboard, (CFIndex)1, &item);

  // find the converter for the first clipboard format we can handle
  IOSXClipboardConverter *converter = nullptr;
  for (ConverterList::const_iterator index = m_converters.begin(); index != m_converters.end(); ++index) {
    converter = *index;

    PasteboardFlavorFlags flags;
    type = converter->getOSXFormat();

    if (converter->getFormat() == format && PasteboardGetItemFlavorFlags(m_pboard, item, type, &flags) == noErr) {
      break;
    }
    converter = nullptr;
  }

  // if no converter then we don't recognize any formats
  if (converter == nullptr) {
    LOG_DEBUG("unable to find converter for data");
    return result;
  }

  // get the clipboard data.
  CFDataRef buffer = nullptr;
  try {
    OSStatus err = PasteboardCopyItemFlavorData(m_pboard, item, type, &buffer);

    if (err != noErr) {
      throw err;
    }

    result = std::string((char *)CFDataGetBytePtr(buffer), CFDataGetLength(buffer));
  } catch (OSStatus err) {
    LOG_DEBUG("exception thrown in OSXClipboard::get MacError (%d)", err);
  } catch (...) {
    LOG_DEBUG("unknown exception in OSXClipboard::get");
    RETHROW_THREADEXCEPTION
  }

  if (buffer != nullptr)
    CFRelease(buffer);

  return converter->toIClipboard(result);
}

void OSXClipboard::clearConverters()
{
  if (m_pboard == nullptr)
    return;

  for (ConverterList::iterator index = m_converters.begin(); index != m_converters.end(); ++index) {
    delete *index;
  }
  m_converters.clear();
}

namespace {

const CFStringRef kFileUrlFlavor = CFSTR("public.file-url");

std::string toUtf8(CFStringRef string)
{
  const auto size = CFStringGetMaximumSizeForEncoding(CFStringGetLength(string), kCFStringEncodingUTF8) + 1;
  std::string buffer(static_cast<size_t>(size), '\0');
  if (!CFStringGetCString(string, buffer.data(), size, kCFStringEncodingUTF8))
    return {};
  buffer.resize(std::strlen(buffer.c_str()));
  return buffer;
}

} // namespace

bool OSXClipboard::hasFileList() const
{
  ItemCount count = 0;
  PasteboardGetItemCount(m_pboard, &count);
  for (CFIndex i = 1; i <= static_cast<CFIndex>(count); ++i) {
    PasteboardItemID item;
    PasteboardFlavorFlags flags;
    if (PasteboardGetItemIdentifier(m_pboard, i, &item) == noErr &&
        PasteboardGetItemFlavorFlags(m_pboard, item, kFileUrlFlavor, &flags) == noErr) {
      return true;
    }
  }
  return false;
}

std::string OSXClipboard::getFileList() const
{
  std::vector<std::string> paths;
  ItemCount count = 0;
  PasteboardGetItemCount(m_pboard, &count);

  for (CFIndex i = 1; i <= static_cast<CFIndex>(count); ++i) {
    PasteboardItemID item;
    CFDataRef data = nullptr;
    if (PasteboardGetItemIdentifier(m_pboard, i, &item) != noErr ||
        PasteboardCopyItemFlavorData(m_pboard, item, kFileUrlFlavor, &data) != noErr) {
      continue;
    }

    CFURLRef url =
        CFURLCreateWithBytes(nullptr, CFDataGetBytePtr(data), CFDataGetLength(data), kCFStringEncodingUTF8, nullptr);
    CFRelease(data);
    if (url == nullptr)
      continue;

    // Finder may hand out file reference URLs (file:///.file/id=...); resolve to a path URL.
    CFURLRef pathUrl = CFURLCreateFilePathURL(nullptr, url, nullptr);
    CFStringRef path = CFURLCopyFileSystemPath(pathUrl != nullptr ? pathUrl : url, kCFURLPOSIXPathStyle);
    if (path != nullptr) {
      // macOS hands back decomposed (NFD) names; Windows and the mapping rules expect NFC.
      CFMutableStringRef composed = CFStringCreateMutableCopy(nullptr, 0, path);
      CFStringNormalize(composed, kCFStringNormalizationFormC);
      paths.push_back(toUtf8(composed));
      if (CFURLHasDirectoryPath(pathUrl != nullptr ? pathUrl : url) && !deskflow::clipboard::isFolder(paths.back()))
        paths.back() += '/';
      CFRelease(composed);
      CFRelease(path);
    }
    if (pathUrl != nullptr)
      CFRelease(pathUrl);
    CFRelease(url);
  }

  return deskflow::clipboard::joinFileList(paths);
}

void OSXClipboard::addFileList(const std::string &payload)
{
  const auto paths = deskflow::clipboard::splitFileList(payload);
  for (size_t i = 0; i < paths.size(); ++i) {
    // The sender marks folders; never stat here, the path is usually on an SMB share.
    const bool isDirectory = deskflow::clipboard::isFolder(paths[i]);

    CFStringRef path = CFStringCreateWithCString(nullptr, paths[i].c_str(), kCFStringEncodingUTF8);
    if (path == nullptr)
      continue;
    CFURLRef url = CFURLCreateWithFileSystemPath(nullptr, path, kCFURLPOSIXPathStyle, isDirectory);
    CFRelease(path);
    if (url == nullptr)
      continue;

    if (CFDataRef data = CFURLCreateData(nullptr, url, kCFStringEncodingUTF8, true); data != nullptr) {
      // One item per file, as Finder does. Item 0 is shared with any text/image flavors.
      const auto itemID = reinterpret_cast<PasteboardItemID>(static_cast<uintptr_t>(i));
      PasteboardPutItemFlavor(m_pboard, itemID, kFileUrlFlavor, data, kPasteboardFlavorNoFlags);
      CFRelease(data);
    }
    CFRelease(url);
  }
  LOG_DEBUG("added %zu files to clipboard", paths.size());
}
