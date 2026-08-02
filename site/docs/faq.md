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

The same "not yet implemented in this build" wording is also reused for a command whose precondition is
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

## Why do some settings need a restart?

Theme, UI language, toolbar icon set and size, tab close buttons, recent-files length, window reuse,
the integrated top bar, its system-native window buttons and (on Linux) the platform-decoration
sharp-corners override are all fixed per process — they are read once while the chrome is being built.
Clicking **OK** in Preferences after changing one of them offers a restart (Cancel discards the changes
instead) — except the two Linux header-bar settings (system-native window buttons and the sharp-corners
override), which only need one when the integrated top bar is on, since they do nothing without it. If
documents have unsaved changes, the save prompt runs first, and the new values are only written once the
restart is actually confirmed, so cancelling out leaves your old settings intact.

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

Legacy `userDefineLang.xml` files are handled by the optional `udl-compat` plugin, which translates
them into Scintillua lexers. If that plugin is not installed, those files do not load. See
[Languages &amp; Syntax](languages.md).

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

## How do I jump to a file without hunting through the tree?

<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>O</kbd> (Quick Open) — type a few letters of the name and press
Enter. It searches your open tabs, your recent files and the whole workspace, and the letters only
have to appear *in order*, so `fmh` finds `fuzzy_match.h`. Type `@` first to jump to a symbol inside
the current file instead.

There is a matching <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>P</kbd> (Command Palette) for commands, which
also shows each command's current shortcut — a quick way to find a keybinding you have forgotten.

Full details in [Quick Open &amp; Command Palette](quick-open.md).

Note <kbd>Ctrl</kbd>+<kbd>P</kbd> is still **Print**, as in Notepad++. Both pickers can be rebound in
[Customizing Shortcuts](custom-shortcuts.md) if you prefer the VS Code arrangement.

## Can I diff two files?

Yes — **Document ▸ Compare ▸ with File…** diffs the current document against another file, and
**with Clipboard** against whatever you last copied. From the shell, `wxnote --diff A B` opens both
and drops straight into the diff, which also makes it usable as a git difftool:

```bash
git config --global difftool.wxnote.cmd 'wxnote --diff "$LOCAL" "$REMOTE"'
```

## Can I run it without touching my settings?

`wxnote --sandbox` opens a window that reads none of your preferences and writes nothing back — no
settings, no window position, no recent-files entry, no crash-recovery backup. It starts from the
built-in defaults and everything it does is discarded when you close it. The title bar is marked
**[Sandbox]** so you can't mistake it for an ordinary window.

Useful for reproducing a bug from a clean slate, trying a theme without committing to it, or
demonstrating the editor on someone else's machine. Plugins still load, so pair it with `--safe` if you
want neither. It is stronger than `--clean`, which skips session restore but still runs against — and
writes back to — your real settings.

## My antivirus says the installer is a trojan

It's a false positive. The Windows builds are not code-signed yet, and an unsigned installer that
nobody has downloaded before has no publisher identity and no reputation — so a scanner falls back on
what the file *looks* like, and a compressed self-extracting installer looks like a lot of things.
Detections with an `!ml` suffix (such as `Wacatac.B!ml`) come from a machine-learning guess, not from
matching known malware.

**The program itself is never flagged — only whichever download happens to have been scored.** We
measured every Windows asset of one release through the same path a browser uses to save a download,
and the block landed on the ARM64 installer and the x64 `.zip`, while the x64 installer and the ARM64
`.zip` came through clean - the same file list built for two architectures, one blocked, one not. Defender's record names
the file as a single object, with nothing inside it named, and `wxnote.exe` and the plugin bridge DLL
pass on their own.

So the practical answer is **try the other download**: every release ships both an installer and a
`.zip` of the same files, and the block rarely hits both. Which one is affected varies by release, so
we cannot tell you in advance which to pick. What the `.zip` costs you is everything the installer does
around the files: the Start Menu shortcut, the Add/Remove Programs entry and its uninstaller, and the
optional "Add to PATH" step. The program itself is identical.

You do not have to take our word for any of this:

- Check the download against the `SHA256SUMS` file shipped with every release
- The build is public — every artifact is produced by GitHub Actions from a tagged commit, logs and all
- The source is Apache-2.0, and every artifact is rebuildable from the tagged commit in public CI

Would signing fix it? It would eventually quiet the separate "unknown publisher" SmartScreen warning —
though not immediately, since a brand-new certificate starts with no reputation of its own — and it is
on the roadmap ([docs/SIGNING.md](https://github.com/Alpaq92/wx-notepad-plus-plus/blob/master/docs/SIGNING.md)).
But it is not a guaranteed cure for this particular kind of detection — GitHub's own CLI installer is
signed, hugely popular, and has been hit by the same detection family. Please don't add an antivirus
exclusion on our account — reporting the false positive to your vendor helps everyone, and helps us.

## Something's wrong and I want to report it

**Help&nbsp;&rsaquo; Debug Info…** shows the wxWidgets version, the OS description and the executable's
path. Include that in the report along with what you did and what happened:
[open an issue](https://github.com/Alpaq92/wx-notepad-plus-plus/issues).
