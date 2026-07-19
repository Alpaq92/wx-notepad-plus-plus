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
| `-e` | `--encoding` | `ansi\|utf8\|utf8bom\|utf16le\|utf16be` | force the encoding used to decode the files |
| `-n` | `--new-instance` | — | always open a new window |
| `-r` | `--reuse-instance` | — | hand the files to an already-running window |
| `-w` | `--wait` | — | do not return until the file is closed |
| | `--safe` | — | start without loading any plugins |
| `-v` | `--version` | — | print the version and exit |
| `-h` | `--help` | — | show the usage message |

Files listed after the options are opened in tabs. Relative paths are resolved against the invoking
process's working directory before anything else happens, which matters when the launch is handed off
to an already-running instance living in a different directory.

`--help` and `--version` print to the invoking console. On Windows the executable is a GUI-subsystem
binary with no console of its own, so it attaches to the parent console to write there; only when there
is no parent console at all (launched from Explorer or the Run box) does the text appear in a message
box instead. `--version` prints a single `wxNote <version>` line and exits with status 0, so
`wxnote --version && …` behaves in scripts.

> **Help&nbsp;&rsaquo; Command Line Arguments…** inside the application shows an abridged summary — only
> `-g`, `-e`, `-n` and `-r`. This page is the complete list.

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

# force a separate window even when "Reuse an existing window" is on
wxnote -n scratch.txt

# block until the window is closed (this is what $EDITOR needs)
wxnote --wait /tmp/message.txt
```

## One window or many

By default every launch opens its own window. Turn on
**Preferences&nbsp;&rsaquo; General&nbsp;&rsaquo; Reuse an existing window** and a second launch instead
hands its file arguments to the first window over IPC and exits immediately.

`-n` and `-r` override that setting for a single launch, in either direction. If a running instance
holds the single-instance lock but does not answer IPC — for example, it crashed mid-startup — the new
launch opens its own window rather than hanging.

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

Two further consequences of the `-w` run being its own dedicated instance:

- **The previous session is not restored.** A `--wait` window opens with just the file you passed, so a
  commit-message edit is not buried under a restored pile of tabs. Your saved session is left untouched
  and comes back on the next ordinary launch.
- **"Ask before closing unsaved changes" is force-enabled for that run only** — otherwise closing the
  window with unsaved edits would silently hand git back an unmodified `COMMIT_EDITMSG`. The setting is
  not written to your preferences; your own choice is preserved.

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

On Linux and macOS there is no such distinction; the shell waits for the foreground process either way.

## Internal switch

`--elevated-write` is an internal Windows helper used by the editor itself when a save needs
administrator rights: the unprivileged process writes its buffer to a temporary file, then relaunches
itself elevated with this switch to perform just the copy — no GUI, no locale or theme setup, nothing
else running elevated. Do not invoke it by hand.
