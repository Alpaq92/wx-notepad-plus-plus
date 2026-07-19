# wxNote User Manual

wxNote is a cross-platform text and source-code editor built on **wxWidgets 3.3** and
**wxStyledTextCtrl** (Scintilla). It runs on Windows, Linux and macOS from one codebase.

This manual documents what the application actually does today. Everything here was written against
the source tree, so if a feature is described as unimplemented or limited, that is a deliberate,
accurate statement rather than an omission.

## Where to start

| If you want to… | Read |
| --- | --- |
| Install it and open your first file | [Getting Started](getting-started.md) |
| Learn the window layout, tabs, panels and split view | [The Interface](interface.md) |
| Find a specific command | [Menu Reference](menus.md) |
| Learn the key bindings | [Keyboard Shortcuts](shortcuts.md) |
| Remap shortcuts, or import Notepad++ keys | [Customizing Shortcuts](custom-shortcuts.md) |
| Configure the editor | [Preferences](preferences.md) |
| Browse a project tree | [Folder as Workspace](workspace.md) |
| Run shell commands without leaving the editor | [Integrated Terminal](terminal.md) |
| Set up syntax highlighting | [Languages &amp; Syntax](languages.md) |
| Change colours | [Themes &amp; Styles](themes.md) |
| Extend the editor, or run Notepad++ plugins | [Plugins](plugins.md) |
| Script it or launch it from a shell | [Command Line](command-line.md) |

## What wxNote is

- **A single, portable codebase.** No Win32 backend; the editor core, the chrome and the dialogs are
  wxWidgets throughout, so Linux and macOS are first-class rather than ports.
- **Scintilla-based.** Editing, folding, multi-selection, rectangular selection, markers and
  lexing all come from the same engine that powers Scintilla-family editors.
- **Localised.** The user interface ships translated into eight languages besides English:
  Polish, German, French, Spanish, Russian, Japanese, Chinese and Korean.
- **Extensible with a permissive plugin API.** The native plugin API ("Nib") is Apache-2.0 and
  cross-platform. Notepad++ binary-plugin support exists, but is deliberately isolated in a separate,
  optional GPL module. See [Plugins](plugins.md).

## Conventions used here

- Menu paths are written as **Menu&nbsp;&rsaquo; Submenu&nbsp;&rsaquo; Item**, e.g.
  **Settings&nbsp;&rsaquo; Preferences…**.
- Keys are written as <kbd>Ctrl</kbd>+<kbd>N</kbd>. On macOS, <kbd>Ctrl</kbd> in a menu accelerator is
  mapped by wxWidgets to the Command key.
- Where a behaviour differs per platform, the platform is named explicitly.

## Project links

- [Source repository](https://github.com/Alpaq92/wx-notepad-plus-plus)
- [Releases and downloads](https://github.com/Alpaq92/wx-notepad-plus-plus/releases)
- [Report an issue](https://github.com/Alpaq92/wx-notepad-plus-plus/issues)
