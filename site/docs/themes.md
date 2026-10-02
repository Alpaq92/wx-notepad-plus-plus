# Themes &amp; Styles

There are two independent appearance settings, and it helps to keep them apart:

- **Theme** (**Preferences&nbsp;&rsaquo; General**) — whether the *application chrome* (menus, dialogs,
  panels, toolbar) is light or dark. System, Dark or Light; applied on restart.
- **Style theme** (**Settings&nbsp;&rsaquo; Style Configurator…**) — the *editor's* colour scheme: the
  syntax-highlighting palette used for text.

## Style Configurator

**Settings&nbsp;&rsaquo; Style Configurator…** opens a three-column editor:

1. **Select theme** — a dropdown of *Default* plus every theme installed: the bundled ones and your own.
2. **Language** — *Global Styles* plus one entry per lexer the theme defines.
3. **Style** — the token styles within the selected language.

For the selected style you can set the **Foreground colour**, the **Background colour**, **Bold** /
**Italic** / **Underline**, and the style's own **font**, **size** and **weight** (blank or 0 inherits
the editor's). (The font settings are disabled for Global Styles, which carry colours only.)

Changes preview live in the editor as you make them. The dialog has **Save &amp; Close** and **Cancel**,
so unlike Preferences you can back out.

### File extensions

Below the lists, the **File extensions** box shows which files open as the selected language:

- **Default ext.** — the extensions wxNote already recognises for it (read-only).
- **User ext.** — your own, separated by spaces: `inc` under *php*, say, or `txt` under *python*.
  Press <kbd>Enter</kbd> to try them on the open document; **Save &amp; Close** keeps them.

Your extensions decide the highlighting, Toggle Comment and the Function List together, and win over
every built-in rule and over a plugin language's own extensions. An extension belongs to one language
at a time, so typing it under another language moves it there. (INI-style files open as *Properties*,
so their extensions go under *props*.) They are kept in wxNote's settings, not in the theme file as in Notepad++,
so they stay when you switch themes (or the theme follows dark/light mode). A theme's own
per-language `extensions` lists (which a theme imported from Notepad++ brings along) are read as well,
but only for extensions wxNote does not already place.

## Bundled themes

28 themes ship with the editor:

Bespin · Black board · Choco · DarkModeDefault · Deep Black · Default · Dracula · GitHub Dark · GitHub Light ·
Hello Kitty · HotFudgeSundae · Mono Industrial · Monokai · MossyLawn · Navajo · Nord · Obsidian ·
One Dark · One Light · Plastic Code Wrap · Ruby Blue · Solarized · Solarized-light · Twilight ·
Vibrant Ink · Zenburn · khaki · vim Dark Blue

Every bundled theme is under a permissive licence (MIT or CC&nbsp;BY&nbsp;3.0). Themes under
non-commercial terms are deliberately not shipped, even when the upstream file's header suggests
otherwise.

## Importing themes

**Settings&nbsp;&rsaquo; Import&nbsp;&rsaquo; Import style theme(s)…** takes one or more wxNote theme
files (`.yaml`) and copies them into your own `themes` folder in the per-user data directory; a file
that does not read as a theme is named in a warning and not copied. The status bar confirms how many
were imported; the new entries then appear in the Style Configurator's theme dropdown. A theme of your
own with the same name as a bundled one takes its place.

A **Notepad++ theme** (`.xml`) comes in through the optional `npp-compat` plugin: open it in wxNote and
run **Extensions&nbsp;&rsaquo; Import the Open Notepad++ File**, or let
**Extensions&nbsp;&rsaquo; Import from Notepad++…** bring across every theme in your Notepad++ settings
folder. Each is translated into a wxNote theme named *&lt;name&gt; (Notepad++)*.

## The theme file

A theme is a YAML file: a `global:` block of named editor styles, and a `lexers:` block with each
language's token styles. A colour is written `'#RRGGBB'`, `fontStyle` takes any of `bold`, `italic`
and `underline`, and a field left out is simply not set — the style then keeps the default's value.

```yaml
# My theme.
global:
  Default Style: {fg: '#24292F', bg: '#FFFFFF', size: 10}
  Current line background colour: {bg: '#F6F8FA'}
lexers:
  python:
    description: Python
    styles:
      - {id: 1, name: COMMENTLINE, fg: '#6E7781', fontStyle: italic}
      - {id: 5, name: KEYWORDS, fg: '#CF222E', fontStyle: bold}
```

The Style Configurator writes its changes back into the theme's file and keeps the comment block at its
top. If a theme file does not parse, the editor falls back to its built-in colours and the status bar
says where the file broke.

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

Cascadia Mono is chosen over Cascadia *Code* deliberately: the two are the same typeface, and Mono is
the variant without programming ligatures. That is a taste decision, not a technical limit — if you
prefer them, install a ligature font such as Cascadia Code or Fira Code and pick it in Preferences.

On Windows they will render, provided **Preferences ▸ Editing ▸ Smoother text rendering (DirectWrite)**
is on, which it is by default. (Before that option existed, text was drawn through GDI, which does no
OpenType shaping — so ligatures genuinely could not appear no matter which font you chose.)

Licensing for both (they are not equally permissive — Cascadia carries a Reserved Font Name) is in the
repository's `resources/fonts/CREDITS.md` and `LICENSING.md`.

## Toolbar icons

Independent of both theme settings, **Preferences&nbsp;&rsaquo; General** offers four toolbar icon
sets — **Tabler** (line, theme-adaptive), **Solar** (green), **IconPark** (teal/lime) and
**Streamline** (green/teal) — at 16, 20, 24 or 32&nbsp;px. Both settings apply on restart. Icons are
SVG, so any size stays sharp.
