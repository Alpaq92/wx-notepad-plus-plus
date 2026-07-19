# FAQ

## I clicked a menu item and the status bar said "not yet implemented in this build"

That message is real and intentional. Rather than letting a click do nothing silently, any menu or
toolbar item without a handler names itself in the status bar. It means exactly what it says: that
command is present in the menu structure but not wired up in this build.

Items that currently behave this way:

- **Go&nbsp;&rsaquo; Change History ▸** — all three entries (Go to Next / Previous Change, Clear Change
  History). This one is blocked by a dependency, not by unfinished work: change history needs upstream
  Scintilla 5.3.0, and wxWidgets vendors its own Scintilla fork that is still at 5.0.0 — so there is
  presently no wxWidgets release to upgrade *to*.
- **Automation&nbsp;&rsaquo; Run&nbsp;&rsaquo; Validate shortcuts.xml** — only when the optional
  `npp-shortcuts-compat` plugin is not installed; the item forwards to that plugin's Notepad++
  shortcut importer. See [Customizing Shortcuts](custom-shortcuts.md#importing-notepad-shortcuts).

The same "not yet implemented in this build" wording is also reused for two other situations that are
not missing features: a command that only exists on Windows (binary clipboard, the SHA/MD5 generators,
Always on Top, the read-only file attribute) invoked elsewhere, and a command whose precondition is
unmet (*"Rename (save the file first)"*, *"Open File (selection is not an existing path)"*). Read the
text in the parentheses — it tells you which case you hit.

## Can I remap keyboard shortcuts?

Yes — **Settings&nbsp;&rsaquo; Shortcut Mapper…** remaps, clears or resets every menu accelerator and
a curated set of the editor's own keys, with live conflict detection. Overrides persist in a
hand-editable `shortcuts.json`, and named keymap schemes (including a **"Notepad++ (imported)"**
scheme created by the optional `shortcuts.xml` import) can be switched in the same dialog. Full detail in
[Customizing Shortcuts](custom-shortcuts.md); the defaults are listed in
[Keyboard Shortcuts](shortcuts.md).

## Does `vim` / `htop` / tab-completion work in the terminal?

Yes. The integrated terminal runs your shell on a real pseudo-terminal (ConPTY on Windows, `forkpty`
on Linux/macOS) with a built-in terminal emulator, so full-screen TUI applications, the shell's own
history and completion, ANSI colour and mouse reporting all work. The one exception is **Windows older
than 10 version 1809**, where ConPTY does not exist: there the tab falls back to the previous
pipe-based console, which handles line-oriented tools only. Details in
[Integrated Terminal](terminal.md#a-real-terminal).

## Preferences has no Cancel button. Is that a bug?

No. The dialog applies and saves on close by design. If you want to undo a change, reopen Preferences
and change it back. The **Style Configurator** *does* have Cancel, because editing a colour scheme is
the kind of exploratory work you may well want to abandon.

## Why do some settings need a restart?

Theme, UI language, toolbar icon set and size, tab close buttons, recent-files length, window reuse and
the integrated top bar are all fixed per process — they are read once while the chrome is being built.
Closing Preferences after changing one of them offers a restart. If documents have unsaved changes, the
save prompt runs first, and the new values are only written once the restart is actually confirmed, so
cancelling out leaves your old settings intact.

## My file opened as garbled characters

Use **Document&nbsp;&rsaquo; Encoding** and pick the correct encoding from the **interpret as** group
(the upper half, including **Character sets ▸**). That re-decodes the bytes already on disk. Do **not**
use the **Convert to…** group for this — that changes the encoding the file will be *written* in and
will bake the damage in. Double-clicking the encoding field in the status bar opens the same choices as
a popup.

## Can I run Notepad++ plugins?

Yes, on every platform — through the optional `npp-bridge` module. On Windows it loads **unmodified
plugin `.dll`s**; on Linux and macOS a precompiled Windows binary cannot be hosted, but the same plugin
**recompiled against the bridge's shim SDK** runs there too (its unchanged `::SendMessage` calls route
into the host). Coverage today: the full Scintilla `SCI_*` surface natively, plus ~30 of the 118
`NPPM_*` messages (file/buffer info, file operations, docking panels, menu commands) — no toolbar
icons or before-save hooks yet. Full detail in [Plugins](plugins.md).

## A plugin is crashing the editor. How do I get back in?

Start with `wxnote --safe`, which loads no plugins at all, then remove the offending file from the
plugin directory. See [Command Line](command-line.md).

## Can I use wxNote as my git commit editor?

Yes — `git config --global core.editor "wxnote --wait"`. `--wait` keeps the process alive until the
window is closed, and forces its own new instance so the handoff to an already-running window can't let
git unblock early and commit the untouched template.

One Windows-only trap: typing `wxnote --wait file` at a bare `cmd.exe` prompt returns instantly anyway,
because `cmd` does not wait on GUI-subsystem programs. That is the shell, not wxNote. Use
`start /wait wxnote --wait file` there. Git itself, `sh` and `make` wait on the process properly, so
the `core.editor` setting above works as written. Details in [Command Line](command-line.md).

## I typed 125% in the zoom box and it changed to 130%

Working as designed. Scintilla zooms by whole **points** on the base font size, not by percent, so only
certain percentages are reachable and the field always shows the one it actually applied — at the usual
10&nbsp;pt base, in 10-point-percentage steps between 20% and 300%. Full explanation in
[Preferences&nbsp;&rsaquo; The zoom control](preferences.md#the-zoom-control).

## My User Defined Language stopped working

wxNote's own built-in UDL engine was removed; legacy `userDefineLang.xml` files are now handled by the
optional `udl-compat` plugin, which translates them into Scintillua lexers. If that plugin is not
installed, those files do not load. See [Languages &amp; Syntax](languages.md).

## Does it lose my work if it crashes?

Unsaved edits are backed up to a recovery directory, and the recovery pass runs on **every** launch —
not only after a clean exit — precisely so that the start after a crash restores the backed-up buffers.

## Where does it keep my settings?

Settings go through `wxConfig` under the application name **wxNote**: the registry on Windows, a config
file under the user's config directory elsewhere. User-writable data — recovery backups, user-defined
languages, `contextMenu.xml`, `shortcuts.json` — lives in the per-user data directory, deliberately
*not* beside the executable, so installed builds work without write access to their install directory.

## What languages is the interface available in?

English plus eight translations: Polish, German, French, Spanish, Russian, Japanese, Chinese and Korean.
Pick one in **Settings&nbsp;&rsaquo; Localization** or
**Preferences&nbsp;&rsaquo; General&nbsp;&rsaquo; Localization**; it applies on restart.

## Can I customise the editor's right-click menu?

Yes — **Settings&nbsp;&rsaquo; Edit Popup ContextMenu** opens `contextMenu.xml` in a tab, seeding your
per-user copy from the shipped one the first time. Edit it and restart.

## Is this Notepad++?

No. wxNote is a separate, ground-up cross-platform editor built on wxWidgets and Scintilla. It shares
Scintilla-family editing behaviour and a familiar command vocabulary, and it can interoperate with some
Notepad++ assets (themes, plugins on Windows, UDL files) through clearly-labelled optional modules — but
the core is original work under a permissive licence and does not link any Notepad++ Win32 code.

## Something's wrong and I want to report it

**Help&nbsp;&rsaquo; Debug Info…** shows the wxWidgets version, the OS description and the executable's
path. Include that in the report along with what you did and what happened:
[open an issue](https://github.com/Alpaq92/wx-notepad-plus-plus/issues).
