# Command Line

The executable is `wxnote` on every platform.

```bash
wxnote [options] [file...]
```

Only `-` introduces an option. A leading `/` is **not** accepted as a switch character, so paths are
never mistaken for options.

## Options

| Short | Long | Argument | Effect |
| --- | --- | --- | --- |
| `-g` | `--goto` | `line[,col]` | open at this line, optionally column, in the last file opened |
| | `--end` | | put the caret at the **end** of the last file opened (wins over `-g`/`+N`; useful for logs) |
| `-e` | `--encoding` | `ansi\|utf8\|utf8bom\|utf16le\|utf16be` | force the encoding used to decode the files |
| `-n` | `--new-instance` | — | always open a new window |
| `-r` | `--reuse-instance` | — | hand the files to an already-running window |
| | `--ns` | — | open each file in its own window, unless the main window already has it open |
| | `--rs` | — | the same as `-r` (the running window never opens a file twice) |
| `-l` | `--language` | `<Language>` | open the files as this language: a **Language** menu name, in any case (`python`, `C++`), or Notepad++'s spelling (`cpp`, `cs`, `bash`, `normal`) |
| `-q` | `--create` | — | create a file that does not exist without asking |
| | `--no-session` | — | neither restore nor save the session; quitting with unsaved changes asks |
| | `--session` | `<file>` | open the files of a session file saved with **File ▸ Save Session…** |
| `-w` | `--wait` | — | do not return until the file is closed |
| `-d` | `--diff` | — | open the two given files side by side and diff them |
| | `--safe` | — | start without loading any plugins |
| | `--clean` | — | like `--safe`, plus `--no-session` |
| | `--sandbox` | — | an independent window that uses **none** of your saved settings, and discards every change it makes |
| | `--locale` | `pl\|de\|ja\|…` | UI language for this run only (does not change your saved preference) |
| `-v` | `--version` | — | print the version and exit |
| `-h` | `--help` | — | show the usage message |

### `--diff`

`wxnote --diff A B` opens both files and puts A into the side-by-side diff against B — the same view
as **Document ▸ Compare ▸ with File…**, without the dialog. It needs exactly two files; given fewer it
just opens them normally.

Handy as a git difftool:

```bash
git config --global difftool.wxnote.cmd 'wxnote --diff "$LOCAL" "$REMOTE"'
git difftool -t wxnote
```

### `--sandbox`

A throwaway window. It reads no preferences, so it starts from wxNote's built-in defaults, and it
writes nothing back — no settings, no window position, no recent-files entry, no crash-recovery
backup. Nothing it does can reach a normal window or survive the process. The title bar is marked
**[Sandbox]** so it can't be mistaken for an ordinary window.

It is always its own instance: it never hands files to a running window, and a later launch will never
be handed into it, whatever `--reuse-instance` or the "Reuse an existing window" preference say.

Use it to reproduce a bug from a clean slate, to try a theme or setting without committing to it, or to
demo the editor on someone else's machine without disturbing their setup. Plugins still load — sandbox
isolates *state*, not code, so combine it with `--safe` if you want neither.

It differs from `--clean` in what it touches: `--clean` skips session restore but still runs against
your real settings, so anything it changes is written back to them. `--sandbox` never touches them at
all.

Files listed after the options are opened in tabs. Relative paths are resolved against the invoking
process's working directory before anything else happens, which matters when the launch is handed off
to an already-running instance living in a different directory.

`--help` and `--version` print to the invoking console. On Windows the executable is a GUI-subsystem
binary with no console of its own, so it attaches to the parent console to write there; only when there
is no parent console at all (launched from Explorer or the Run box) does the text appear in a message
box instead. `--version` prints a single `wxNote <version>` line and exits with status 0, so
`wxnote --version && …` behaves in scripts.

> **Help&nbsp;&rsaquo; Command Line Arguments…** inside the application lists the same options, in the
> same order, as a quick reference. This page adds the explanations and examples.

## Examples

```bash
# open two files
wxnote notes.txt build.log

# jump straight to line 240, column 12
wxnote -g 240,12 src/main.cpp

# a log file that was written as Windows-1250, misdetected as UTF-8
wxnote -e ansi legacy.log

# diagnose a misbehaving plugin
wxnote --safe

# try something out without touching your settings — nothing here is saved
wxnote --sandbox

# diff two files straight from the shell
wxnote --diff old.conf new.conf

# force a separate window even when "Reuse an existing window" is on
wxnote -n scratch.txt

# a file with no telling extension, as Python; a new file without the "Create it?" question
wxnote -l python Jenkinsfile.groovy
wxnote -q todo.md

# block until the window is closed (this is what $EDITOR needs)
wxnote --wait /tmp/message.txt
```

## One window or many

There is one window by default, as in Notepad++: a second launch hands its file arguments to the first
window over IPC and exits immediately. Turn off
**Preferences&nbsp;&rsaquo; General&nbsp;&rsaquo; Reuse an existing window** and every launch opens its
own window instead.

`-n` and `-r` override that setting for a single launch, in either direction. If a running instance
holds the single-instance lock but does not answer IPC — for example, it crashed mid-startup — the new
launch opens its own window rather than hanging.

`--ns` gives each file a window of its own, as Notepad4's `/ns` does: a file the main window already has
open is brought to the front there, and every other one opens in a new window.

### The main window and the session

Only the **main window** — the first one started — has a session. It reopens your files when it starts,
remembers them when it quits, keeps the crash-recovery copies of unsaved work and is the window later
launches hand their files to.

Any other window (`-n`, `--ns`, or with reuse turned off) starts empty, leaves the session alone and,
having nowhere to keep unsaved changes for next time, asks to save them when you quit. The same goes for
`--no-session`, `--clean`, `--wait` and `--sandbox` windows, which never become the main window even
when they are the first: the next ordinary launch still gets your session.

## A missing file

A file named on the command line that does not exist is offered for creation: answer **Yes** and wxNote
creates it empty and opens it. `-q` / `--create` skips the question.

## Notepad++'s options

Scripts and shortcuts written for Notepad++ keep working. Its spellings are read as wxNote's:

| Notepad++ | wxNote |
| --- | --- |
| `-multiInst` | `-n` |
| `-nosession` | `--no-session` |
| `-openSession <file>` | `--session <file>` |
| `-ro` | `-R` |
| `-l<Language>` (`-lpython`) | `-l <Language>` |
| `-n<line>` `-c<column>` | `-g line,column` |
| `-noPlugin` | `--safe` |
| `-pluginMessage=<text>` | `--pluginMessage=<text>` |

Its window and printing options (`-notabbar`, `-systemtray`, `-alwaysOnTop`, `-x`, `-y`, `-p`, `-L`,
`-titleAdd=`, `-settingsDir=`, `-udl=`, `-monitor`, `-loadingTime`, `-quickPrint`, `-qn`/`-qt`/`-qf`) are
accepted and ignored, so the files on such a command line still open. Notepad++'s `-r` (open the files of
a wildcard recursively) is the one clash: in wxNote `-r` reuses the running window, as in Notepad4.
`-ns` and `-rs` with a single dash work too.

## Using wxNote as an editor for other tools

`--wait` is what makes wxNote usable as `$EDITOR` for git, `crontab` and friends: those tools expect the
editor process to block until the file is closed.

```bash
git config --global core.editor "wxnote --wait"
```

`--wait` **implies `-n`**, and overrides `-r` if you pass both. That is deliberate: the IPC handoff
path executes and exits immediately, so without forcing a new instance git would unblock before the tab
had even opened and would commit the untouched template. Forcing a new instance reduces "block until
closed" to "this process stays alive", with no reply channel, no proxy event loop, and no way to hang
if another instance dies.

Two further consequences of the `-w` run being its own dedicated instance, which is never the main
window:

- **The previous session is not restored.** A `--wait` window opens with just the file you passed, so a
  commit-message edit is not buried under a restored pile of tabs. Your saved session is left untouched
  and comes back on the next ordinary launch.
- **"Ask to save unsaved changes when quitting" is force-enabled for that run only** — otherwise closing
  the window with unsaved edits would keep them for the next launch and silently hand git back an
  unmodified `COMMIT_EDITMSG`. The setting is not written to your preferences; your own choice is
  preserved.

### Windows: `cmd.exe` will not wait

`wxnote` is a GUI-subsystem executable, and a bare `cmd.exe` prompt (or a `.bat` file) does **not**
block on GUI-subsystem processes — it returns to the prompt the instant the process is launched. This
is a shell behaviour, not something `--wait` can override. Use `start /wait`:

```bat
start /wait wxnote --wait notes.txt
```

Tools that wait on the process handle themselves are unaffected and block correctly: **git**, `sh`
(Git Bash, MSYS2, Cygwin, WSL), `make`, and anything else that does the equivalent of
`CreateProcess` + `WaitForSingleObject`. So `git config --global core.editor "wxnote --wait"` works on
Windows as written — the caveat only bites when *you* type the command at a `cmd` prompt.

On Linux, macOS and FreeBSD there is no such distinction; the shell waits for the foreground process either way.

## Saving to a protected location (Windows)

wxNote has **no internal elevation switch and never relaunches itself with administrator rights.**
When a save target needs rights the editor does not have, it stages the bytes in a temporary file and
asks Windows to move them into place through the shell's own file-operation service — the same
mechanism File Explorer uses. Windows shows its standard shielded consent dialog naming the
destination, and the privileged step happens inside the shell, not inside wxNote.

(Earlier versions did have a `--elevated-write` helper that re-launched the editor elevated. It was
removed in 0.15.0: it performed an unvalidated file copy with administrator rights, which made
`wxnote.exe` usable as an arbitrary elevated-write tool by anything already running as the user.)
