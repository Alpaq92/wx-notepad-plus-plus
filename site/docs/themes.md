# Themes &amp; Styles

There are two independent appearance settings, and it helps to keep them apart:

- **Theme** (**Preferences&nbsp;&rsaquo; General**) — whether the *application chrome* (menus, dialogs,
  panels, toolbar) is light or dark. System, Dark or Light; applied on restart.
- **Style theme** (**Settings&nbsp;&rsaquo; Style Configurator…**) — the *editor's* colour scheme: the
  syntax-highlighting palette used for text.

## Style Configurator

**Settings&nbsp;&rsaquo; Style Configurator…** opens a three-column editor:

1. **Select theme** — a dropdown of *Default* plus every theme XML installed.
2. **Language** — *Global Styles* plus one entry per lexer the theme defines.
3. **Style** — the token styles within the selected language.

For the selected style you can set the **Foreground colour**, the **Background colour**, and **Bold** /
**Italic**. (Bold and Italic are disabled for Global Styles, which carry colours only.)

Changes preview live in the editor as you make them. The dialog has **Save &amp; Close** and **Cancel**,
so unlike Preferences you can back out.

## Bundled themes

27 themes ship with the editor:

Bespin · Black board · Choco · DarkModeDefault · Deep Black · Dracula · GitHub Dark · GitHub Light ·
Hello Kitty · HotFudgeSundae · Mono Industrial · Monokai · MossyLawn · Navajo · Nord · Obsidian ·
One Dark · One Light · Plastic Code Wrap · Ruby Blue · Solarized · Solarized-light · Twilight ·
Vibrant Ink · Zenburn · khaki · vim Dark Blue

Every bundled theme is under a permissive licence (MIT or CC&nbsp;BY&nbsp;3.0). Themes under
non-commercial terms are deliberately not shipped, even when the upstream file's header suggests
otherwise.

## Importing themes

**Settings&nbsp;&rsaquo; Import&nbsp;&rsaquo; Import style theme(s)…** takes one or more `.xml` theme
files and copies them into the editor's `themes` directory. The status bar confirms how many were
imported; the new entries then appear in the Style Configurator's theme dropdown.

The format is the familiar `stylers`-style XML, so Notepad++ theme files generally import directly.

## Editor colours not covered by the theme

A few appearance settings live in **Preferences&nbsp;&rsaquo; Editing** instead, because they are
preferences rather than palette entries:

- **Use a custom line-number margin colour** — off by default, in which case the gutter follows the
  active theme. When on, the accompanying colour picker applies immediately, with no restart.
- **Highlight current line**, **Caret width**, **Caret blink rate** and the **vertical edge column**.

## The editor font

Five monospace families are **bundled with the editor**, so the same font is available on Windows, Linux
and macOS without installing anything:

| Font | Role |
| --- | --- |
| **Cascadia Mono** | the **default**, and the face the editor falls back to if the configured font is missing |
| **JetBrains Mono** | the second bundled choice |
| **IBM Plex Mono** | humanist coding face (SIL OFL 1.1) |
| **Hack** | DejaVu-heritage workhorse (MIT + Bitstream Vera) |
| **Iosevka Fixed** | narrow / condensed — more code per line (SIL OFL 1.1) |

Both are pinned to the top of the **Preferences&nbsp;&rsaquo; Editing&nbsp;&rsaquo; Font** dropdown,
above a divider line, with every installed system font listed alphabetically below it. Both ship in
Regular and Bold, which is what lets bold syntax styles use a real bold face instead of a
mechanically-widened one that would break column alignment.

The fonts are registered **for the running process only** — nothing is installed system-wide and no
administrator rights are involved, on any platform.

The font *size* is not a preference: it comes from the theme (the bundled themes all declare 10&nbsp;pt),
and zoom adjusts it from there. That is also why zoom percentages are quantised — see
[The zoom control](preferences.md#the-zoom-control).

Cascadia Mono is chosen over Cascadia *Code* deliberately: Code's programming ligatures would never be
drawn anyway, because Scintilla renders through GDI on Windows and GDI does no OpenType shaping.

Licensing for both (they are not equally permissive — Cascadia carries a Reserved Font Name) is in the
repository's `resources/fonts/CREDITS.md` and `LICENSING.md`.

## Toolbar icons

Independent of both theme settings, **Preferences&nbsp;&rsaquo; General** offers four toolbar icon
sets — **Tabler** (line, theme-adaptive), **Solar** (green), **IconPark** (teal/lime) and
**Streamline** (green/teal) — at 16, 20, 24 or 32&nbsp;px. Both settings apply on restart. Icons are
SVG, so any size stays sharp.
