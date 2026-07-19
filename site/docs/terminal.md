# Integrated Terminal

**View&nbsp;&rsaquo; Show Terminal** — <kbd>Ctrl</kbd>+<kbd>`</kbd> — opens a dockable terminal pane at
the bottom of the window, holding a notebook of shell tabs. The chord works from the editor; once the
terminal itself owns the keyboard the frame's accelerators are deliberately suspended (plain
<kbd>Ctrl</kbd>+<kbd>C</kbd> must stay the shell's interrupt), so from inside the terminal the panel is
closed with its own chrome — the collapse button — rather than the same chord.

## Opening shells

- The **+** button opens another tab running the currently selected shell.
- The **shell picker** beside it lists the shells this machine actually has — the list is probed at
  startup, not hardcoded:

| Platform | Shells offered |
| --- | --- |
| Windows | `cmd` and `PowerShell` always; `pwsh`, **Cygwin** and **WSL** when they are installed |
| Linux | your `$SHELL` first, then every common shell found on `PATH` |
| macOS | your `$SHELL` first, then every common shell found on `PATH` |

The picker is keyboard-navigable: <kbd>Up</kbd>/<kbd>Down</kbd> move (wrapping at the ends),
<kbd>Home</kbd>/<kbd>End</kbd> jump, <kbd>Enter</kbd> picks and <kbd>Esc</kbd> dismisses.

## Reaching the chrome from the keyboard

The terminal itself consumes <kbd>Tab</kbd> — it belongs to the shell — so there would otherwise be no
way to leave the terminal for its toolbar. Two chords bridge that gap:

| Keys | Effect |
| --- | --- |
| <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Up</kbd> | move focus from the terminal up to the panel's toolbar |
| <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Down</kbd> | return focus to the active terminal |

Once focus is on the toolbar, <kbd>Tab</kbd> / <kbd>Shift</kbd>+<kbd>Tab</kbd> move between its
buttons, and <kbd>Enter</kbd> or <kbd>Space</kbd> activates the focused one; the focused button draws a
focus ring. A chord rather than <kbd>F6</kbd> is used on purpose, because `cmd.exe` binds
<kbd>F6</kbd> to <kbd>Ctrl</kbd>+<kbd>Z</kbd>.

New tabs start in **the active document's directory**, so a terminal opened while editing a project
file is already where you need it.

Terminal tabs render in the same font you chose in **Preferences&nbsp;&rsaquo; Editing&nbsp;&rsaquo;
Font** — by default the bundled **Cascadia Mono**. The choice is read when a terminal tab is created,
so change it first, then open the tab.

## A real terminal

Each tab is a **genuine pseudo-terminal** — ConPTY on Windows, `forkpty` on Linux and macOS — rendered
by a built-in terminal emulator (libvterm, the same core Neovim embeds). The shell believes it is
talking to a real TTY, because it is. That means:

- **Full-screen TUI applications work**: `vim`, `htop`, `less`, menu-driven installers, anything that
  addresses the screen.
- **The shell's own line editing works**: command history with <kbd>Up</kbd>, tab completion,
  <kbd>Ctrl</kbd>+<kbd>R</kbd> search — these are TTY features, and there is now a TTY.
- **Colour is rendered**, not stripped: the 16 ANSI colours (themed to match the terminal's light/dark
  setting — the *lights* toggle recolours existing output retroactively, scrollback included), the
  256-colour palette, bold/italic/underline/reverse.
- **Mouse reporting** is forwarded when an application asks for it (`vim` with `mouse=a`, `htop`
  clicking). Hold <kbd>Shift</kbd> to bypass the application and select text locally, xterm-style.
- The terminal resizes with the pane, and the shell is told about it (`SIGWINCH` /
  `ResizePseudoConsole`), so TUI apps reflow.

Scrollback holds 5,000 lines; scroll with the mouse wheel, and any keypress snaps back to the live
screen. Select with the mouse, copy with <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>C</kbd>, paste with
<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>V</kbd> — plain <kbd>Ctrl</kbd>+<kbd>C</kbd> deliberately stays
what a terminal means by it: interrupt the running command.

On Windows, `cmd` sessions are switched to UTF-8 (`chcp 65001`) so non-ASCII output isn't mojibake;
the pseudo-terminal byte stream itself is UTF-8 end to end.

## Fallback on old Windows

ConPTY — the Windows pseudo-terminal API — exists on **Windows 10 version 1809 and later**. On older
Windows (or if the pseudo-terminal cannot be created for any reason), the tab quietly falls back to
the previous **redirected-pipe console**: line-oriented tools (git, compilers, scripts, package
managers) work, ANSI colour is stripped rather than rendered, and full-screen TUI apps and the shell's
own line editing are unavailable. The panel, tabs, picker and shortcuts are identical in both modes.

## Remaining limits

- **IME composition** (e.g. Japanese/Chinese input methods) is not supported in the terminal view yet.
- Double-width characters occupy their two cells correctly, but complex-script and CJK/emoji glyph
  *shaping* is basic — the grid is functional rather than typographically pretty there.
- Double and curly underlines render as a single underline.

## Closing

Closing a tab terminates its shell and the whole process tree (Windows) or process group (Linux and
macOS) it spawned, so a background child cannot outlive the tab it was started from.
