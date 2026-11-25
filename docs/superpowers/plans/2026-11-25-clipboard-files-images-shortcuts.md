# Clipboard files, PNG images and shortcut translation: implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Copy files and images on the Mac, paste them on Windows (and the reverse), and turn chosen Mac
shortcuts into Windows shortcuts while the cursor is on Windows.

**Architecture:**
- Files: two new clipboard formats, `FileList` (NUL-separated paths) and `PNG`. The receiving machine
  rewrites file paths to its SMB view in `Computer::setClipboard`, so Explorer or Finder does the copying.
- Images: PNG bytes travel on the wire. Each OS converts to and from its native image format.
- Shortcuts: a client-side table in `ServerProxy`, checked before the modifier remap.

**Tech stack:** C++20, Qt 6 (Core, Gui for `QImage`, QtTest), Carbon Pasteboard API (macOS), Win32
clipboard (Windows), CMake + Ninja.

**Spec:** `docs/superpowers/specs/2026-11-25-clipboard-file-copy-design.md`

## Global Constraints

- Fork of deskflow `master` @ `80221f0f7`. Work on branch `clipboard-files`.
- Push only to the `fork` remote (`tuanchris/deskflow`). Never push to `origin`, and never open PRs or
  issues.
- Keep changes in new files where possible. Edits to existing upstream files stay minimal so the fork
  rebases easily.
- A peer running stock Deskflow must keep working for text and HTML. Unknown formats are skipped by
  `IClipboard::unmarshall` (`src/lib/deskflow/IClipboard.cpp:63`).
- Settings keys:
  - `core/clipboardFileMappings`: a `QStringList` of `sourcePrefix|targetPrefix`.
  - `core/shortcutMappings`: a `QStringList` of `from|to`.
  - Both default to empty and must be registered in `Settings::validKeys`.
- Change the `server/clipboardSize` default from 3 to 32 MiB.
- Style: SPDX header on new files (`SPDX-FileCopyrightText: (C) 2026 Deskflow Developers`,
  `SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception`, copied from a neighbouring file).
- Mac build:
  - Configure with
    `cmake -S. -Bbuild -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH="$(brew --prefix qt);$(brew --prefix openssl@3)" -DBUILD_OSX_BUNDLE=OFF -DWrapOpenGL_AGL="$(xcrun --show-sdk-path)/System/Library/Frameworks/OpenGL.framework"`.
  - The last flag works around Homebrew Qt 6.8.2 linking `AGL`, which the macOS 27 SDK no longer has.
- Tests:
  - Run one test with `ctest --test-dir build/src/unittests -R <Name> --output-on-failure`.
  - Baseline: 26 of 30 pass. These 4 failures are environmental and existed before this work:
    `LoggerTests`, `ComputerTests`, `NetworkMonitorTests` (no Qt cocoa plugin) and `OSXKeyStateTests`
    (no Accessibility).
- Windows code compiles only in fork CI (`continuous-integration.yml`, started by `workflow_dispatch`).

## Review Focus

1. **Paths with spaces and non-ASCII characters** (`a file.txt`, `résumé.pdf`, `照片`) survive split, join,
   mapping and both OS converters. Covered by tests in Tasks 2, 4 and 5.
2. **A folder copied in Finder.** Its URL ends in `/`. After conversion it maps to a UNC path without a
   trailing `\`, and Explorer pastes the whole folder. Covered in Tasks 2 and 4.
3. **A Finder copy carries both the file list and the file name as text.** Text must still arrive. When no
   rule matches, the file list is dropped and every other format still arrives. Covered in Task 2
   (`applyFileMappings_noMatch_keepsOtherFormats`).
4. **Shortcuts with Shift** (`super+shift+z|control+y`). With Shift held, the server sends `KeyID` `'Z'`, so
   matching must ignore case for letters. Covered in Task 6.
5. **A corrupt or empty PNG payload** returns empty and never crashes. The Windows converter then writes
   nothing for that format. Covered in Task 3.

---

### Task 1: Add the `FileList` and `PNG` clipboard formats

**Files:**
- Modify: `src/lib/deskflow/IClipboard.h:49-55`, the `Format` enum. Add `FileList` (value 3) and `PNG`
  (value 4) before `TotalFormats`, each with a one-line doc comment.
- Modify: `src/lib/deskflow/Clipboard.h:62-63` and `src/lib/platform/EiClipboard.h:50-51`. Replace the
  3-element initialisers with `{}`.
- Modify: `src/lib/platform/XWindowsClipboard.h`. Arrays are sized by `TotalFormats`, so nothing changes;
  confirm it still compiles.
- Test: `src/unittests/deskflow/ClipboardTests.{h,cpp}`

**Interfaces:**
- Produces: `IClipboard::Format::FileList` and `IClipboard::Format::PNG`.

- [ ] **Step 1: Write the failing test** `marshall_fileListAndPng_roundTrips`.
  - Fill a `Clipboard` with `Text = "x"`, `FileList = std::string("/a b\0/c", 7)` and
    `PNG = std::string("\x89PNG\r\n\x1a\n", 8)`.
  - Marshall it, then unmarshall into a fresh `Clipboard`.
  - `QCOMPARE` each `get(...)` and `has(...)`.
- [ ] **Step 2: Run** `ctest --test-dir build/src/unittests -R ClipboardTests --output-on-failure`.
  Expected: it fails to compile because `FileList` isn't defined.
- [ ] **Step 3: Implement the enum and initialiser changes** listed above.
- [ ] **Step 4: Rebuild and run `ClipboardTests`.** Expected: PASS. Run all tests: still 26 of 30.
- [ ] **Step 5: Commit**: `feat(clipboard): add FileList and PNG formats`

### Task 2: File-path mapping, applied when a clipboard is received

**Files:**
- Create: `src/lib/deskflow/ClipboardFileList.h`, `src/lib/deskflow/ClipboardFileList.cpp`. Add both to the
  sources in `src/lib/deskflow/CMakeLists.txt`.
- Modify: `src/lib/common/Settings.h`:
  - Add `Core::ClipboardFileMappings = "core/clipboardFileMappings"` and
    `Core::ShortcutMappings = "core/shortcutMappings"`.
  - Add both to the `validKeys` list.
- Modify: `src/lib/common/Settings.cpp`, in `defaultValue`: `ClipboardSize` returns `32`. The new keys
  return `QStringList{}`.
- Modify: `src/lib/deskflow/Computer.cpp:207`. `Computer::setClipboard` calls `applyFileMappings` on a copy
  before passing it on.
- Test: create `src/unittests/deskflow/ClipboardFileListTests.{h,cpp}` and register it with `create_test` in
  `src/unittests/deskflow/CMakeLists.txt`, using `LIBS arch base io ${extra_libs}` like `ClipboardTests`.

**Interfaces:**
- Consumes: `IClipboard::Format::FileList` (Task 1).
- Produces, all in `namespace deskflow::clipboard`:
  - `std::vector<std::string> splitFileList(std::string_view payload);`
  - `std::string joinFileList(const std::vector<std::string> &paths);`
  - `std::vector<std::string> mapFilePaths(const std::vector<std::string> &paths, const QStringList &rules, bool targetIsWindows);`
  - `void applyFileMappings(Clipboard &clipboard, const QStringList &rules, bool targetIsWindows);`
    Rewrites `FileList` in place. If nothing maps, it removes the format and keeps every other format.
  - `QStringList fileMappingRules();` reads `Settings::Core::ClipboardFileMappings`.
  - `constexpr bool kTargetIsWindows`, true when `_WIN32` is defined.

- [ ] **Step 1: Write the failing tests.** Each one calls `mapFilePaths` unless noted.

| Test | Input | Expected |
|-|-|-|
| `split_join_roundTrip` | `{"/a b", "/é/照片"}` | Join then split gives the input back. `splitFileList("")` is empty. |
| `map_macToWindows_uncAndBackslashes` | rule `/Users/me\|//mac.local/me`, path `/Users/me/Docs/a b.pdf`, Windows target | `\\mac.local\me\Docs\a b.pdf` |
| `map_prefixBoundary_noPartialMatch` | rule `/Users/me\|//m/t`, path `/Users/mex/a` | empty result |
| `map_exactFolder_matches` | path `/Users/me` | `\\m\t` |
| `map_longestPrefixWins` | rules `/Volumes\|//m/v` and `/Volumes/SSD\|//m/ssd`, path `/Volumes/SSD/x` | `\\m\ssd\x` |
| `map_windowsToMac_caseInsensitive` | rule `C:/Users/me\|/Volumes/me`, path `c:\users\ME\Desktop\x.png`, Mac target | `/Volumes/me/Desktop/x.png` |
| `map_trailingSlashInRule_ignored` | rule `/Users/me/\|//m/t/` | Same result as without the trailing slashes. |
| `map_badRule_skipped` | rule `no-separator`, any path | empty result, no crash |
| `applyFileMappings_noMatch_keepsOtherFormats` | `Clipboard` with `Text` and an unmatched `FileList` | `has(FileList)` is false; `Text` is unchanged |
| `applyFileMappings_rewritesPayload` | one mapped path | `get(FileList) == joinFileList({mapped})` |

- [ ] **Step 2: Run** `ctest … -R ClipboardFileListTests`. Expected: build fails, because the file doesn't
  exist yet.
- [ ] **Step 3: Implement.**
  - Normalise each rule side and each path to `/` separators and strip trailing separators.
  - A rule side is "Windows" when it starts with `X:` or `//`. Compare those case-insensitively, using
    `QString::compare(…, Qt::CaseInsensitive)`.
  - A match needs `path == prefix`, or `path` to start with `prefix + "/"`. The longest prefix wins.
  - Replace the prefix, then convert to `\` when `targetIsWindows`.
  - Log each unmatched path with `LOG_WARN("clipboard file not under any mapping: %s", path)`.
  - `applyFileMappings`: `Clipboard` has no remove method. Rebuild with `empty()` and re-`add` the other
    formats.
  - `Computer::setClipboard`:
    - When `clipboard->has(FileList)` is true and the clipboard is not `nullptr`, copy it into a local
      `Clipboard` with `Clipboard::copy`.
    - Then call `applyFileMappings(copy, fileMappingRules(), kTargetIsWindows)` and pass `&copy` on.
    - Otherwise pass the original through unchanged.
- [ ] **Step 4: Run `ClipboardFileListTests`, `ClipboardTests` and `SettingsTests`.** Expected: PASS.
- [ ] **Step 5: Commit**: `feat(clipboard): map received file paths to SMB locations`

### Task 3: PNG ↔ DIB helpers

**Files:**
- Create: `src/lib/deskflow/ClipboardImage.{h,cpp}`. Add them to the deskflow lib sources, and link that
  lib `PRIVATE Qt6::Gui`.
- Test: create `src/unittests/deskflow/ClipboardImageTests.{h,cpp}` and register it.

**Interfaces:**
- Produces, in `namespace deskflow::clipboard`:
  - `std::string dibToPng(std::string_view dib);`
    The input is a BMP without its 14-byte file header, the same layout as the `Format::Bitmap` payload.
    Returns empty on failure.
  - `std::string pngToDib(std::string_view png);`
    The output is in the same no-file-header layout, 32-bit `BI_RGB`. Returns empty on failure.

- [ ] **Step 1: Write the failing tests.**
  - `roundTrip_24bit_preservesPixels`:
    - Build a 3×2 `QImage::Format_RGB888` with known colours.
    - Save it as `"BMP"` into a `QByteArray` and drop the first 14 bytes to get the DIB.
    - Run `pngToDib(dibToPng(dib))`, prepend a BMP file header, and load the result.
    - Expect every pixel to equal the original (compare `pixel()` with alpha ignored).
  - `roundTrip_32bit_preservesPixels`: the same, with `Format_RGB32`.
  - `dibToPng_garbage_returnsEmpty`: input `"xyz"`.
  - `pngToDib_garbage_returnsEmpty`: input `""` and input `"\x89PNG"`.
- [ ] **Step 2: Run** `ctest … -R ClipboardImageTests`. Expected: build fails.
- [ ] **Step 3: Implement.**
  - `dibToPng`:
    - Build a 14-byte `BITMAPFILEHEADER`: `'BM'`, the file size, and `bfOffBits` from the same pixel-offset
      rule as `OSXClipboardBMPConverter::dibPixelOffset`.
    - Call that helper, after moving it into `ClipboardImage.cpp` as `uint32_t dibPixelOffset(const uint8_t*, size_t)`.
      `OSXClipboardBMPConverter` keeps a one-line forwarder.
    - Load with `QImage::loadFromData(…, "BMP")`, then `save` as `"PNG"` into a `QBuffer`.
  - `pngToDib`: `QImage::loadFromData(…, "PNG")`, convert to `Format_RGB32`, `save` as `"BMP"`, and strip
    14 bytes.
- [ ] **Step 4: Run `ClipboardImageTests` and `OSXClipboardTests`.** The BMP test still passes through the
  forwarder. Expected: PASS.
- [ ] **Step 5: Commit**: `feat(clipboard): PNG/DIB conversion helpers`

### Task 4: macOS file lists and PNG

**Files:**
- Create: `src/lib/platform/OSXClipboardPNGConverter.{h,cpp}`. It passes `public.png` through unchanged;
  copy the shape of `OSXClipboardHTMLConverter`.
- Modify: `src/lib/platform/OSXClipboard.cpp`:
  - In the constructor, register `OSXClipboardPNGConverter` and remove the `OSXClipboardBMPConverter` line.
  - `has`, `get` and `add` return early for `FileList`, with three private helpers:
    `bool hasFileList() const;`, `std::string getFileList() const;`, `void addFileList(const std::string &payload);`.
  - At the end of `add`, call `PasteboardSynchronize(m_pboard)`.
  - Skip `PasteboardPutItemFlavor` when a converter returns empty data.
- Modify: `src/lib/platform/OSXClipboard.h`: declare the helpers.
- Modify: `src/lib/platform/CMakeLists.txt`: add the new sources. Link `platform PRIVATE Qt6::Gui` on
  APPLE and WIN32, as Linux already does.
- Test: `src/unittests/platform/OSXClipboardTests.{h,cpp}`

**Interfaces:**
- Consumes: `FileList` and `PNG` (Task 1). `splitFileList` and `joinFileList` (Task 2).
- `getFileList()`:
  - Walk items 1…N from `PasteboardGetItemCount`, taking the `public.file-url` flavor of each.
  - Build the URL with `CFURLCreateWithBytes` and resolve it with `CFURLCreateFilePathURL`.
  - Take `CFURLCopyFileSystemPath(…, kCFURLPOSIXPathStyle)` as UTF-8, then `joinFileList`.
- `addFileList()`:
  - Item id `i + 1` for each path. Set `isDirectory` from `stat`.
  - Use `CFURLCreateWithFileSystemPath`, then `CFURLCreateData`.
  - This is the exact sequence the spike proved (spec §4).

- [ ] **Step 1: Write the failing tests.** Tests use the real pasteboard, as the existing ones do.
  - `fileList_addThenGet_roundTrips`:
    - Create a temp dir (`QTemporaryDir`) holding `a file.txt`, `résumé.pdf` and a sub-folder.
    - `open`, `empty`, then `add(FileList, joinFileList(paths))`, then `close`.
    - `get(FileList)` returns the same three paths, folder included, with no trailing `/`.
    - `has(FileList)` is true.
  - `png_addThenGet_passthrough`: add an 8-byte PNG signature plus 4 bytes. `get(PNG)` returns the same
    bytes. `has(Bitmap)` is false.
  - `text_unaffectedByFileList`: add `Text` and a `FileList`. Both read back.
- [ ] **Step 2: Run** `ctest … -R OSXClipboardTests`. Expected: the new tests FAIL.
- [ ] **Step 3: Implement** as described in Files and Interfaces.
- [ ] **Step 4: Run `OSXClipboardTests`.** Expected: PASS. Run the full suite: still only the 4 baseline
  failures.
- [ ] **Step 5: Manual check on this Mac:**
  - Copy two files and a folder in Finder.
  - Run a tiny harness, or check `deskflow-core` debug logs, to confirm that `getFileList` returns their
    POSIX paths.
  - Take a screenshot with Cmd+Ctrl+Shift+4 and confirm `has(PNG)`.
- [ ] **Step 6: Commit**: `feat(macos): clipboard file lists and PNG images`

### Task 5: Windows file lists and PNG

**Files:**
- Create: `src/lib/platform/MSWindowsClipboardFileListConverter.{h,cpp}` for `CF_HDROP`.
- Create: `src/lib/platform/MSWindowsClipboardPNGConverter.{h,cpp}`. It uses
  `RegisterClipboardFormat(L"PNG")` and copies the bytes into and out of an `HGLOBAL` unchanged.
- Create: `src/lib/platform/MSWindowsClipboardDIBPNGConverter.{h,cpp}` for `CF_DIB`. It owns a
  `MSWindowsClipboardBitmapConverter` and wraps it with `dibToPng` and `pngToDib`.
- Modify: `src/lib/platform/MSWindowsClipboard.cpp`:
  - Converter list order: UTF16, HTML, FileList, PNG, DIBPNG. Remove the `Bitmap` converter line.
  - In `add`, remove the `break`, so every converter for a format writes.
  - In `get`, choose the first converter of that format whose `IsClipboardFormatAvailable(getWin32Format())`
    is true.
  - Add the new sources to `PLATFORM_SOURCES` in `src/lib/platform/CMakeLists.txt`.
- Test: `src/unittests/platform/MSWindowsClipboardTests.{h,cpp}`. These only run in CI.

**Interfaces:**
- Consumes: Tasks 1–3.
- `MSWindowsClipboardFileListConverter`:
  - `toIClipboard(HANDLE)` uses `DragQueryFileW` on the `HDROP`. Convert each path from UTF-16 to UTF-8
    with `QString::fromWCharArray(...).toStdString()`, then `joinFileList`.
  - `fromIClipboard(const std::string&)` builds `DROPFILES{sizeof(DROPFILES), {0,0}, FALSE, TRUE}` followed
    by NUL-terminated UTF-16 paths and a final NUL, in `GlobalAlloc(GMEM_MOVEABLE)`. It returns `nullptr`
    for an empty list.
- Link `shell32`, which provides `DragQueryFileW`, if the platform lib doesn't already.

- [ ] **Step 1: Write the failing tests** in `MSWindowsClipboardTests`, in the existing open, empty, add,
  get style.
  - `fileList_roundTrip`: paths `C:\Temp\a b.txt` and `C:\Temp\résumé`. Read back identical.
  - `png_writesBothPngAndDib`:
    - `add(PNG, pngBytes)`, built with `QImage` 2×2 saved as PNG.
    - Then `IsClipboardFormatAvailable(CF_DIB)` and the registered `PNG` format are both true.
    - `get(PNG)` returns the original bytes, because the passthrough is preferred.
  - `dibOnly_getPng_transcodes`: put a `CF_DIB` built by `pngToDib` on the clipboard with the facade.
    `get(PNG)` is a valid PNG that decodes to 2×2.
- [ ] **Step 2: Implement** as described in Files and Interfaces.
- [ ] **Step 3: Verify in CI after Task 7 pushes:** the `windows-2022-x64` job is green, including
  `MSWindowsClipboardTests`. A local failure can't happen, because there is no local Windows build.
- [ ] **Step 4: Commit**: `feat(windows): clipboard file lists (CF_HDROP) and PNG images`

### Task 6: Shortcut translation on the receiving machine

**Files:**
- Create: `src/lib/client/ShortcutMap.{h,cpp}`. Add them to the client lib sources.
- Modify: `src/lib/client/ServerProxy.{h,cpp}`:
  - Add a member `ShortcutMap m_shortcuts`, loaded in the constructor from
    `Settings::value(Settings::Core::ShortcutMappings).toStringList()`.
  - In `keyDown` (line 577) and `keyRepeat` (line 589), check `m_shortcuts.match(rawId, rawMask)` before
    `translateKey` and `translateModifierMask`.
  - On a hit, call `m_client->keyDown(hit->id, hit->mask, button, lang)` and skip the remap. `keyUp` is
    unchanged.
- Test: create `src/unittests/client/ShortcutMapTests.{h,cpp}` and register it in
  `src/unittests/client/CMakeLists.txt`, copying the `ServerProxyTests` entry.

**Interfaces:**
- Produces:

```cpp
struct Shortcut { KeyID id; KeyModifierMask mask; };
class ShortcutMap {
public:
  explicit ShortcutMap(const QStringList &rules = {});        // "from|to"; bad entries LOG_WARN + skipped
  std::optional<Shortcut> match(KeyID id, KeyModifierMask mask) const;
};
```

- Parse with `KeyMap::parseModifiers(std::string&, KeyModifierMask&)`, then `KeyMap::parseKey`.
- For comparison:
  - Keep only Shift, Control, Alt, AltGr, Meta and Super in the mask. Lock bits are ignored.
  - Compare `KeyID`s lowercased for `'A'`–`'Z'`.

- [ ] **Step 1: Write the failing tests.**

| Test | Setup | Expected |
|-|-|-|
| `match_superQ_returnsAltF4` | rule `super+q\|alt+F4`; call `match('q', KeyModifierSuper)` | `{kKeyF4, KeyModifierAlt}` |
| `match_ignoresLockBits` | the same rule; mask `KeyModifierSuper \| KeyModifierCapsLock \| KeyModifierNumLock` | a hit |
| `match_shiftedLetter_caseInsensitive` | rule `super+shift+z\|control+y`; call `match('Z', Super\|Shift)` | `{'y', Control}` |
| `match_extraModifier_noHit` | rule `super+q`; call `match('q', Super\|Shift)` | `nullopt` |
| `match_namedKeys` | rules `super+Left\|Home` and `alt+BackSpace\|control+BackSpace` | both hit |
| `badRule_skipped` | rules `garbage` and `super+q\|` | no crash; `match` returns `nullopt` |

- [ ] **Step 2: Run** `ctest … -R ShortcutMapTests`. Expected: build fails.
- [ ] **Step 3: Implement** `ShortcutMap` and the `ServerProxy` hook.
- [ ] **Step 4: Run `ShortcutMapTests` and `ServerProxyTests`.** Expected: PASS.
- [ ] **Step 5: Commit**: `feat(client): per-machine shortcut translation`

### Task 7: Docs, CI trigger, fork, push and Windows build

**Files:**
- Modify: `docs/Configuration.md`:
  - Add the two settings with the examples from spec §2 and §6.
  - Add the SMB setup steps and the security note from the spec.
  - Add PNG image sharing and the 32 MiB default.
  - Add the known shortcut limits.
- Modify: `.github/workflows/continuous-integration.yml`. Add `workflow_dispatch:` under `on:`. This is a
  fork-only commit.

- [ ] **Step 1: Write the docs.** Every setting name and example must match the code exactly. Grep for
  `clipboardFileMappings` and `shortcutMappings` in both places.
- [ ] **Step 2: Commit the docs**: `docs: clipboard files, PNG images, shortcut mappings`. Commit the CI
  change separately: `ci(fork): allow manual workflow runs`.
- [ ] **Step 3: Create the fork and push.**
  - Run `gh repo fork deskflow/deskflow --remote --remote-name fork --clone=false`.
  - Confirm `git remote -v` shows `fork` pointing at `tuanchris/deskflow`.
  - Run `git push -u fork clipboard-files`.
- [ ] **Step 4: Start the Windows build.**
  - Actions are disabled on new forks until the owner enables them in the fork's Actions tab. Ask them to.
  - Then run `gh workflow run continuous-integration.yml --repo tuanchris/deskflow --ref clipboard-files`.
- [ ] **Step 5: Watch the run** with `gh run watch --repo tuanchris/deskflow`. Expected: the Windows and
  macOS jobs are green.
  - Fix any Windows compile errors from Task 5 and push again.
  - Download the `package-*-windows-2022-x64` artifact path for the owner.
- [ ] **Step 6: Hand over the acceptance checklist** for the owner to run on the real machines (spec,
  Verification):
  - Files both ways: one file, several files, a folder, and a 2 GB file.
  - A path outside every mapping.
  - Images both ways: Paint, Chrome or Slack, and Preview.
  - Cmd+Q → Alt+F4 and Cmd+Left → Home on Windows.
  - Cmd+Q still quits apps on the Mac.
