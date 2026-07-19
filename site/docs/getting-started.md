# Getting Started

## Installing

Grab a build for your platform from the
[Releases page](https://github.com/Alpaq92/wx-notepad-plus-plus/releases), or use the download
buttons on the [wxNote home page](../), which read the latest release live from the GitHub API.

| Platform | Package |
| --- | --- |
| Windows | NSIS installer (`.exe`) — x64 and ARM64 |
| macOS | Disk image (`.dmg`) — Apple Silicon (`arm64`) and Intel (`x86_64`) |
| Linux | `.AppImage`, `.deb`, `.rpm` and `.flatpak` — x64 and ARM64, plus a RISC-V `.deb` |

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
