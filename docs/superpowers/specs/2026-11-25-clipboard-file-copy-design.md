# Clipboard file copy (Mac ↔ Windows) — design

Date: 2026-11-25 · Status: approved by owner ("go ahead and build"), images added the same day · Fork of
deskflow `master` @ `80221f0f7`

## Intent

Personal use, one Mac and one Windows PC sharing a keyboard and mouse through Deskflow. The owner wants to
copy files the normal way on one machine (Cmd+C in Finder / Ctrl+C in Explorer), move the cursor to the
other machine, and paste (Ctrl+V / Cmd+V) to get a copy of the files there. It should feel like copying
between two folders on one machine: no extra app, no dragging into a share window.

Upstream removed its old drag-and-drop file transfer in `5365e34f0` ("broken on all platforms"). This
design does not revive it.

## Goals

- Copy one or more files and folders on the Mac, paste them in Explorer on Windows. And the reverse.
- Any file size. Large copies show the OS's own progress UI.
- Mouse and keyboard never stall because of a file copy.

## Non-goals

- Linux / X11 / Wayland support (the new format is simply ignored there).
- Drag and drop between screens.
- Move semantics: Explorer "cut" pastes as a copy.
- A GUI for the path mapping: it is edited by hand in the settings file.
- Moving file bytes over the Deskflow connection.
- Upstreaming. (Agents may not open PRs or issues on deskflow; see `AGENTS.md`.)

## Anti-goals (failure even if files copy)

- Text or HTML clipboard sharing gets worse or breaks.
- Copying an image stalls the mouse or keyboard when crossing screens.
- A peer running stock Deskflow breaks or logs errors when it receives the new format.
- The fork becomes hard to rebase on upstream (large edits spread across existing files).

## Constraints found in the code

- The clipboard is marshalled whole into memory and queued as chunks ahead of input events
  (`StreamChunker::sendClipboard`). File contents cannot go through it without stalling input. Paths can.
- A stock peer skips unknown format ids (`src/lib/deskflow/IClipboard.cpp:63`), so a new format stays
  backward compatible.
- The Mac clipboard (`src/lib/platform/OSXClipboard.cpp`) uses the Carbon Pasteboard Manager API and
  reads only item 1. Finder puts one item per copied file.
- The Windows clipboard uses one converter per format (`IMSWindowsClipboardConverter`: Win32 format id,
  `HANDLE` ↔ `std::string`). CF_HDROP fits it as is.
- `Settings::cleanSettings()` deletes unknown keys in known groups, so a new key must be registered in
  `Settings::validKeys`.
- No local Windows toolchain. Windows builds come from the repo's GitHub Actions
  (`continuous-integration.yml` builds and uploads `windows-2022-x64`), which needs a GitHub fork.
  The Mac is built locally.

## Approaches

| | How | Trade-off |
|-|-|-|
| **A. Paths + SMB (chosen)** | Deskflow sends only file paths. The receiving machine rewrites each path to where the source folder is reachable over SMB and puts that on its clipboard. Explorer/Finder do the copy. | Instant to send, any size, folders work, a few hundred lines, protocol unchanged apart from one new format. Needs one-time SMB setup on both machines; files outside the shared folders don't paste. |
| B. Fetch on paste | New protocol messages; Windows virtual files (`CFSTR_FILEDESCRIPTOR`/`CFSTR_FILECONTENTS` + `IStream`), Mac file promises. | No SMB, any file. Weeks of work, touches the protocol, hardest to rebase. Unclear whether Finder accepts promised files on paste. |
| Rejected: send contents with the clipboard | File bytes inside the clipboard payload. | Stalls input for big files and resends on every screen switch. |

Chosen by the owner: **A**. They had already offered to set up SMB.

## Design

### 1. New clipboard format

- Add `IClipboard::Format::FileList` (value 3) before `TotalFormats`.
- Payload: absolute paths as the source OS spells them, UTF-8, separated by `\0` (NUL cannot appear in a
  path on either OS). Example from the Mac: `/Users/me/a.pdf\0/Users/me/Photos`.
- Update the fixed-size array initialisers that list one value per format (`Clipboard.h`,
  `EiClipboard.h`) and the format logging in `OSXClipboard::add`.

### 2. Path mapping (receiving side)

New pure function in new files `src/lib/deskflow/ClipboardFileMapping.{h,cpp}`:

```cpp
// Returns the mapped paths in target-OS form; drops paths no rule matches.
std::vector<std::string> mapClipboardFilePaths(
    const std::vector<std::string> &sourcePaths,
    const QStringList &rules,       // "sourcePrefix|targetPrefix"
    bool targetIsWindows);
```

- The longest matching source prefix wins. The match must end on a path boundary: `/Users/me` must not
  match `/Users/mex`.
- Comparison treats `/` and `\` as equal. It ignores case when the source prefix is a Windows path
  (drive letter or leading `//`).
- After replacing the prefix, convert every separator to the target's style. So rules use forward slashes
  everywhere and nobody has to escape backslashes in the ini file. A UNC prefix is written `//host/share`.
- An unmatched path is dropped with a `LOG_WARN` naming it. If nothing matches, the receiver does not set a
  file list at all.

New setting `core/clipboardFileMappings`, a `QStringList`, read in the core process through
`Settings::value`. Register it in `Settings::validKeys` with an empty default.

Example on the Windows PC (paths copied on the Mac):

```ini
[core]
clipboardFileMappings=/Users/me|//my-mac.local/me, /Volumes/SSD|//my-mac.local/SSD
```

Example on the Mac (paths copied on Windows; the share is mounted at `/Volumes/me`):

```ini
[core]
clipboardFileMappings=C:/Users/me|/Volumes/me
```

The host names, user names and folders above are placeholders. The real values are the owner's
configuration, not code.

### 3. Windows: `MSWindowsClipboardFileListConverter` (new files)

- Win32 format: `CF_HDROP`.
- `toIClipboard(HANDLE)`: read the `DROPFILES` block (`DragQueryFileW`), convert each path from UTF-16 to
  UTF-8, join with `\0`.
- `fromIClipboard(string)`: split, map with `targetIsWindows = true`, then build `DROPFILES`
  (`pFiles = sizeof(DROPFILES)`, `fWide = TRUE`) followed by NUL-terminated UTF-16 paths and a final NUL,
  in `GlobalAlloc(GMEM_MOVEABLE)` memory. Return `nullptr` when nothing mapped. The builder checks that
  `MSWindowsClipboard::add` tolerates `nullptr`.
- Register it next to the existing converters in `MSWindowsClipboard`.

### 4. Mac: special case inside `OSXClipboard`

One file is one pasteboard item, so this doesn't fit the one-item converter interface. Handle `FileList`
directly in `OSXClipboard`:

- `has` / `get`: walk every item (`PasteboardGetItemCount`) and collect those carrying `public.file-url`.
  Resolve each URL with `CFURLCreateFilePathURL` first, because Finder may hand out file-reference URLs
  (`file:///.file/id=…`). Then take `CFURLCopyFileSystemPath(…, kCFURLPOSIXPathStyle)`.
- `add`: map with `targetIsWindows = false`. For each path, put a `public.file-url` flavor on its own item
  id, using the bytes of `CFURLCreateWithFileSystemPath` (set `isDirectory` from `stat`).
- The existing formats keep reading item 1 unchanged.
- Call `PasteboardSynchronize` after writing. Spike (2026-11-25, macOS 27): without it, a short-lived process
  exited before the write reached the pasteboard server. With it, two Carbon-written `public.file-url` items
  (a file with a space in its name, and a folder) read back through
  `NSPasteboard readObjectsForClasses:[NSURL]` as file URLs. The owner then pressed Cmd+V in Finder and
  both items pasted.

Builder's call: the file list is sent whenever file URLs or `CF_HDROP` are present, even when an image or
text rides along. The alternative, dropping the file list when an image is present, risks killing plain
Finder copies if Finder ever adds an icon image to them. The cost of always sending it is small: an app
that prefers files pastes the same image as a file attachment.

### 5. Images: PNG on the wire (added 2026-11-25 at the owner's request: "images copied to clipboard shared instantly")

Two things found in the code and on the owner's Mac:

- The Mac only reads and writes `com.microsoft.bmp`. A macOS screenshot copied to the clipboard
  (Cmd+Ctrl+Shift+4) is one item carrying only `public.png`, verified on macOS 27. So Mac → Windows images
  don't work today.
- `Format::Bitmap` is an uncompressed DIB. A Retina screenshot as 32-bit BMP is about 20 MB. That is over
  the 3 MiB default `server/clipboardSize`, so `Server.cpp:499` drops the whole clipboard without a word.
  Raising the limit alone would stall input on every screen switch.

The design:

- Add a new format, `IClipboard::Format::PNG` (value 4). Its payload is PNG file bytes, roughly 5–10× smaller
  than BMP.
- On the Mac, a new `OSXClipboardPNGConverter` passes `public.png` through unchanged. It replaces
  `OSXClipboardBMPConverter` in the registration list. The BMP converter class and its tests stay, just
  unregistered, which keeps the diff small for rebasing.
- On Windows, two new converters handle `PNG`, registered in this order:
  1. `MSWindowsClipboardPNGConverter`: the registered `"PNG"` clipboard format, passed through. Chrome,
     Office, Slack and Snipping Tool use it.
  2. `MSWindowsClipboardDIBPNGConverter`: `CF_DIB`, transcoded. It reuses the existing
     `MSWindowsClipboardBitmapConverter` for `HANDLE` ↔ DIB and the helpers below for DIB ↔ PNG. This
     covers apps that only offer or only accept `CF_DIB`, such as Paint.

  `MSWindowsClipboardBitmapConverter` is unregistered.
- `MSWindowsClipboard` gets two small edits:
  - `add` writes every converter registered for the format, instead of stopping after the first. Today each
    format has one converter, so existing behaviour doesn't change.
  - `get` picks the first converter whose Win32 format is actually available.
- New shared helpers in `src/lib/deskflow/ClipboardImage.{h,cpp}`: `dibToPng` and `pngToDib`, built on
  `QImage`. QtGui is already linked into `deskflow-core` through `Qt6::Widgets`. On Mac and Windows, link
  `platform` to `Qt6::Gui` PRIVATE, as Linux already does.
- Raise the default `server/clipboardSize` from 3 to 32 MiB.

Image non-goals:

- Mac sources that offer only TIFF, not PNG.
- Alpha transparency through `CF_DIB`.
- Images to and from stock Deskflow peers. The fork no longer sends or reads `Bitmap`, and stock ignores
  `PNG`. Text and HTML still interoperate.

### 6. Per-machine shortcut translation (added 2026-11-25: "match a shortcut from mac to another on windows")

Goal: pressing a Mac shortcut while the cursor is on Windows sends a different Windows shortcut. For example
Cmd+Q sends Alt+F4, and Cmd+Left sends Home. When the cursor is on the Mac, nothing changes.

The existing hotkeys can't do this. `keystroke(super+q) = keystroke(alt+F4,winpc)` is registered as an OS
hotkey on the server, so it would also swallow Cmd+Q while the cursor is on the Mac. Hotkeys are global, not
per active screen.

Approaches:

| | How | Trade-off |
|-|-|-|
| **Client-side table (builder's call)** | The receiving machine looks up each incoming key-down and key-repeat in its own rule list, in `ServerProxy`, before the modifier remap. On a match it passes the replacement key and mask to `Client::keyDown` and skips the remap. | Touches one existing file plus new files. Needs no server, protocol or GUI change. Only applies while the cursor is on that machine. The rules live in the same settings file as the file mappings. |
| Screen-scoped server hotkeys | Add `keystroke(key, computers)` conditions, and register or unregister the OS hotkeys on every screen switch. | Touches the config parser, `InputFilter`, `Server::switchComputer` and the GUI hotkey dialog. More upstream files to rebase. Risky timing when a switch happens with a key held. |

Design:

- New setting `core/shortcutMappings`, a `QStringList` of `from|to` entries. It lives on the machine that
  receives the keys (the Windows PC when the Mac is the server).
- Example: `super+q|alt+F4, super+Left|Home, super+Right|End, alt+Left|control+Left`.
- The left side is written as pressed on the source keyboard, before any modifier remap, so in Mac terms.
  The right side is written in the target's terms, after remap, so in Windows terms.
- Parse both sides with `KeyMap::parseModifiers` and `KeyMap::parseKey`. Log and skip entries that don't
  parse.
- Matching: the `KeyID` must be equal, comparing letters case-insensitively. The mask must be equal on
  Shift, Control, Alt, AltGr, Meta and Super, ignoring the lock keys.
- On a match, `ServerProxy` passes the replacement `KeyID` and mask, with the original `KeyButton`.
  - Key-up is unchanged. The screen releases whatever that button pressed.
  - Repeat uses the same lookup.
- Why the held modifiers don't leak: `KeyMap::mapCommandKey` overrides the active modifiers with the
  requested mask (`s_overrideModifiers = 0xffff`). So the remapped Ctrl, which is still held from Cmd, is
  released while Alt+F4 is sent. The builder verifies this on Windows.
- Pure logic goes in new files `src/lib/client/ShortcutMap.{h,cpp}`, with a unit test.

Known limits, to check during acceptance and document:

- Cmd+Tab → Alt+Tab probably won't keep the Windows switcher open, because modifiers are restored after
  the key press.
- macOS may catch some system shortcuts (Cmd+Tab, Cmd+Space, Cmd+Shift+3/4, Ctrl+arrows) before Deskflow
  forwards them.
- One key in, one key out. Sequences like "select to line start, then delete" aren't supported.

### 7. Things that stay unchanged

The protocol version, the X11/Ei/Portal clipboards (they have no converter for the new formats and ignore
them), and the GUI.

## SMB setup (owner, one time; goes in the docs)

1. Mac: System Settings → General → Sharing → File Sharing. Share the home folder and `/Volumes/SSD` to
   your user only.
2. Windows: share `C:\Users\<user>` (or narrower) to your account only.
3. Windows: save the Mac login so Explorer never prompts:
   `cmdkey /add:<mac-host>.local /user:<mac-user> /pass`.
4. Mac: mount `smb://<pc-host>/<share>` and add it to Login Items so `/Volumes/<share>` exists after every
   login.

**Security:** File Sharing serves the shared folders, home included, to every network the Mac joins.
Use strong account passwords. Turn File Sharing off on networks you don't trust, such as public Wi-Fi.

## Verification

- **Spike first, before anything else:** on macOS 27, write `public.file-url` items with the Carbon
  Pasteboard API and confirm that Cmd+V in Finder copies the files. Also confirm that a Finder copy yields
  paths that resolve. If Finder refuses, stop and revisit the Mac half before building more.
- Unit test for `mapClipboardFilePaths`: prefix boundary, longest match, case-insensitive Windows prefix,
  separator conversion, unmatched paths dropped. Add it under `src/unittests/deskflow`, in the existing
  test style.
- Round-trip tests in the existing `OSXClipboardTests` and `MSWindowsClipboardTests`. The Windows test runs
  in CI.
- Unit test for `dibToPng` / `pngToDib`: a 24-bit and a 32-bit DIB survive a round trip pixel for pixel.
  It runs on the Mac.
- Manual image checks, both directions:
  - Mac → Windows: Cmd+Ctrl+Shift+4, then paste into Paint (exercises `CF_DIB`) and into Chrome or Slack
    (exercises `PNG`).
  - Windows → Mac: Win+Shift+S, then paste into Preview (New from Clipboard) and into Slack.
  - A full Retina screenshot crosses screens with no visible input lag.
- Manual acceptance, both directions: one file, several files, a folder, a 2 GB file (input stays smooth),
  a path outside every mapping (warning logged, text clipboard unaffected), and a stock Deskflow peer
  (connects, no errors).

## Build and release

- Setup: the Mac is the Deskflow server and the Windows PC is the client. The keyboard and mouse are on the
  Mac.
- Owner approved (2026-11-25):
  - Create the fork `tuanchris/deskflow` and add it as a git remote named `fork`.
  - Commit to the branch `clipboard-files` and push it to the fork.
  - In the fork only, add `workflow_dispatch:` to the triggers in `.github/workflows/continuous-integration.yml`,
    so a Windows build can be started from the Actions tab. Upstream only builds on pushes to master and
    tags, and on PRs.
  - The fork's `master` stays a mirror of upstream.
- Install the `windows-2022-x64` artifact on the PC.
- Build the Mac locally. Each ad-hoc-signed rebuild will probably need Accessibility granted again, unless
  it is signed with a stable identity.
- Only the fork's `clipboard-files` branch may be pushed to. Never push to `origin` (deskflow/deskflow), and
  never open PRs or issues.

## Docs that ship with the change

- `docs/Configuration.md`: the `core/clipboardFileMappings` and `core/shortcutMappings` settings, the SMB
  setup steps above, PNG image sharing, and the new 32 MiB `clipboardSize` default.

## Left to the builder

The pasteboard item-id scheme, whether settings are re-read on every paste or cached, log wording, and the
exact test-file layout.
