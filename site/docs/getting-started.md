# Getting Started

## Installing

Grab a build for your platform from the
[Releases page](https://github.com/Alpaq92/wx-notepad-plus-plus/releases), or use the download
buttons on the [wxNote home page](../), which read the latest release live from the GitHub API.

| Platform | Package |
| --- | --- |
| Windows | NSIS installer (`.exe`) — x64 and ARM64, plus a plain `.zip` of the same files |
| macOS | Disk image (`.dmg`) — Apple Silicon (`arm64`) and Intel (`x86_64`) |
| Linux | `.AppImage`, `.deb`, `.rpm` and `.flatpak` — x64 and ARM64, plus a RISC-V `.deb` |

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

It only ever affects the **installer stub**, never the editor: `wxnote.exe` and the `.zip` that
contains it have been measured clean in the same download where the installer was quarantined. **If
you hit this, download the `.zip` instead** — identical files, no installer stub, and you can look
inside before running anything. The trade-off is no Start Menu shortcut and no Add/Remove Programs
entry; extract it wherever you like and run `wxnote.exe`.

[docs/ANTIVIRUS.md](https://github.com/Alpaq92/wx-notepad-plus-plus/blob/master/docs/ANTIVIRUS.md)
documents every privileged and network-touching thing wxNote does, and what it deliberately does not,
so the claim can be checked rather than taken on faith.

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
