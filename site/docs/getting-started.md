# Getting Started

## Installing

Grab a build for your platform from the
[Releases page](https://github.com/Alpaq92/wx-notepad-plus-plus/releases), or use the download
buttons on the [wxNote home page](../), which read the latest release live from the GitHub API.

| Platform | Package |
| --- | --- |
| Windows | NSIS installer (`.exe`) — x64, ARM64 and 32-bit x86, each with a matching `.zip` of the same files |
| macOS | Disk image (`.dmg`) — Apple Silicon (`arm64`) and Intel (`x86_64`) |
| Linux | `.AppImage`, `.deb`, `.rpm` and `.flatpak` — x64 and ARM64, plus a RISC-V `.deb` |

On Windows, take **x64** unless you know otherwise — it is the right answer for essentially every
machine sold in the last fifteen years, and it runs on ARM64 Windows too, under emulation. Take
**ARM64** on a Windows-on-ARM laptop (Snapdragon X, older SQ-series Surface) for a native, faster,
cooler-running build. Take **x86** only if you are on a genuinely 32-bit Windows, where nothing else
will run; note that a 32-bit process is limited to a few GB of memory, which matters for very large
files.

### Verifying your download

Every release ships a `SHA256SUMS` file listing the checksum of each artifact. On Windows:

```powershell
Get-FileHash .\wxNote-<version>-Setup.exe -Algorithm SHA256
```

and compare that against the matching line in `SHA256SUMS`. On macOS and Linux, `shasum -a 256 -c
SHA256SUMS` checks everything at once.

### "Windows protected your PC", or an antivirus warning

The Windows builds are **not code-signed yet**, so SmartScreen shows an "unknown publisher" warning,
and an antivirus may report a generic machine-learning detection such as `Wacatac.B!ml` on a
freshly-published release. This is a false positive caused by the missing signature and by the file
being brand new — not by anything the program does.

It never affects the editor itself — only whichever download happened to be scored. **If you hit
this, take the other download**: every release ships both an installer and a `.zip` of exactly the same
files, and the block rarely lands on both.

The `.zip` is the same program, just the files on their own. What you give up is everything the
installer does *around* them: the Start Menu shortcut, the Add/Remove Programs entry with a real
uninstaller, and the optional "Add to PATH" step that lets you run `wxnote` from any shell. Extract it
wherever you like and run `wxnote.exe`.

The executable is named `wxnote` on every platform. Resources (themes, icons, fonts, translations,
bundled plugins) live next to the executable rather than being split across system directories.

## First launch

wxNote opens with a single untitled document, `new 1`.

If the previous run exited cleanly with files open, those files are reopened and the empty startup
document is dropped. Unsaved edits are additionally backed up to a recovery directory, and the
recovery pass runs on **every** launch — so if the editor is killed or crashes, the next start
restores the backed-up buffers rather than losing them.

## Opening files

- **File&nbsp;&rsaquo; Open…** (<kbd>Ctrl</kbd>+<kbd>O</kbd>).
- Drag and drop files onto the window.
- Pass paths on the command line: `wxnote file1.txt file2.log` — see [Command Line](command-line.md).
- Double-click a file in the [Folder as Workspace](workspace.md) tree.
- Open the folder containing the current file with **File&nbsp;&rsaquo; Open Containing Folder**, which
  also offers the file managers, terminals and shells that are actually installed on the machine.

Each open document gets a tab. Tabs can be reordered, moved between the two editor views, colour-tagged
and pinned.

## Saving

| Action | Menu | Key |
| --- | --- | --- |
| Save | File&nbsp;&rsaquo; Save | <kbd>Ctrl</kbd>+<kbd>S</kbd> |
| Save under a new name | File&nbsp;&rsaquo; Save As… | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>S</kbd> |
| Save a copy, keeping the current file open | File&nbsp;&rsaquo; Save a Copy As… | — |
| Save every modified document | File&nbsp;&rsaquo; Save All | — |

On Windows, if a save fails because the file needs administrator rights, wxNote writes the buffer to a
temporary file and relaunches a minimal, GUI-less helper instance elevated to perform just the copy.
Nothing else ever runs elevated.

By default, closing a modified document discards it silently. Turn on
**Preferences&nbsp;&rsaquo; General&nbsp;&rsaquo; "Ask before closing unsaved changes"** if you want a
Save / Don't Save / Cancel prompt instead.

## Sessions

**File&nbsp;&rsaquo; Save Session…** writes an XML file listing the open documents. Each entry stores
more than the path: the caret position, the first visible line and the bookmarked lines are all
preserved, so **File&nbsp;&rsaquo; Load Session…** restores where you actually were, not just which
files were open.

Session files written by Notepad++ (with a `<NotepadPlus>` root element) load fine — the loader does
not check the root tag name.

## Next steps

- [The Interface](interface.md) — panels, split view, status bar.
- [Keyboard Shortcuts](shortcuts.md) — the full accelerator list.
- [Customizing Shortcuts](custom-shortcuts.md) — remap keys, switch keymap schemes, import
  Notepad++ shortcuts.
- [Preferences](preferences.md) — every setting, page by page.
