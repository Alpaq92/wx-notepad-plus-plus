# Preferences

**Settings&nbsp;&rsaquo; Preferences…** opens a dialog with a page list on the left and eight pages:
**General**, **Editing**, **Indentation**, **Auto-Completion**, **New Document**, **Tab Bar**,
**Recent Files History** and **Print**.

> **OK** applies and saves every change; **Cancel** (or Esc, or the window's own close button) discards
> them all and leaves every setting exactly as it was. Settings that cannot change inside a running
> process (marked *restart* below) prompt for a restart only after OK.

---

## General

| Setting | Default | Notes |
| --- | --- | --- |
| Show toolbar | on | |
| Show status bar | on | |
| Show zoom control in status bar | **off** | adds an editable zoom field to the status bar — see [below](#the-zoom-control). Applies immediately, no restart. Zooming itself is unaffected either way |
| Ask before closing unsaved changes | **off** | when off, closing a modified document discards it silently |
| Auto-hide toolbar in full screen | **off** | when on, full screen hides the toolbar (macOS-style) |
| Reuse an existing window | off | *restart* — when on, a second launch hands its files to the first window over IPC and exits. `-n` / `-r` override this per launch |
| Show integrated top bar | — | *restart* — only present on platforms with borderless-window support and on macOS |
| System-native window buttons | **off** | *restart* — Windows and Linux only (macOS always keeps its native traffic lights), and only affects the integrated top bar. The buttons stay in the same single bar, in place of the custom ones — this just swaps their glyphs for the platform's own. **Windows**: the bar and its buttons are also handed back to the OS, so Windows&nbsp;11 snap layouts pop up over the maximize button, dragging/snapping/Aero&nbsp;Shake and double-click-maximize become the OS's own, and Windows&nbsp;11's Segoe Fluent Icons glyphs are used where installed. **Linux**: the minimize/maximize/close glyphs come from your desktop theme's own symbolic icons (falling back to the bundled glyphs if the theme lacks them) |
| Localization | English | *restart* — the UI language; the same list as **Settings&nbsp;&rsaquo; Localization** |
| Toolbar icon style | Tabler icons (line) | *restart* — also Solar icons (green), IconPark icons (teal/lime) and Streamline icons (green/teal) |
| Toolbar icon size | — | *restart* — 16, 20, 24 or 32&nbsp;px |
| Theme | System | *restart* — System, Dark or Light |

### The zoom control

Turning on **Show zoom control in status bar** puts an editable percentage field into status-bar
field 6, immediately — no restart. Turning it back off collapses that field to zero width. The setting
controls the *readout* only: <kbd>Ctrl</kbd>+mouse wheel and
**View&nbsp;&rsaquo; Zoom&nbsp;&rsaquo; Zoom In / Zoom Out / Restore Default Zoom** work identically
whether the control is shown or not.

Three ways to drive it:

- **Type a percentage** and press <kbd>Enter</kbd> (the `%` sign is optional). <kbd>Esc</kbd> abandons
  the edit. Empty or nonsensical input silently reverts to the current value — a status-bar field never
  raises a dialog.
- **Click the ▼** for a preset list, which opens *upward* out of the status bar. Clicking the caret
  again closes it.
- **Roll the mouse wheel** over the field for one zoom step per notch.

#### Why the number you get back may not be the number you typed

Scintilla's zoom is a **point-size delta applied to the default style — not a percentage**. The editor
adds or subtracts whole points, so the reachable percentages are quantised by the base font size, and
the field always snaps to and redisplays the value it actually applied.

The bundled themes declare a 10 pt base (the raw built-in default is 11 pt), so at a 10 pt base:

- one zoom step is 1 pt, i.e. **10 percentage points** — there is nothing between 100% and 110%;
- typing `125%` lands on **130%**, and the field will say so;
- the presets are relabelled with the percentage they genuinely produce, and duplicates are dropped —
  at a 10 pt base the list reads 50 · 80 · 100 · 130 · 150 · 180 · 200 · 250 · 300%.

The range is bounded at both ends:

- **Ceiling: +20 steps** (300% at a 10 pt base). This clamp is not cosmetic. Scintilla's own zoom-in
  message refuses to act above a zoom level of 20, so a value parked past it would permanently kill
  <kbd>Ctrl</kbd>+wheel-up and **Zoom In** while zoom-out kept working.
- **Floor: whatever brings the rendered text to 2 pt** (20% at a 10 pt base), and never more than 10
  steps down. Scintilla stops shrinking at 2 pt without lowering its zoom level any further, so going
  below that would make the percentage readout lie about what is on screen.

A different base size shifts the whole grid: with the built-in 11 pt base the same field spans roughly
18% to 282%, in steps of about 9 points. If you change the editor font size through a theme, expect the
reachable percentages to move with it.

## Editing

| Setting | Notes |
| --- | --- |
| Font | **Cascadia Mono** by default. The two bundled faces (**Cascadia Mono**, then **JetBrains Mono**) are pinned at the top of the list, above a divider, followed by every installed system font in alphabetical order. If the chosen face is later uninstalled — or the divider line itself is somehow selected — rendering falls back to **Cascadia Mono**. See [Fonts](themes.md#the-editor-font) |
| Display line number | |
| Show indentation guide | |
| Show white space and TAB | |
| Show wrap symbol | |
| Word wrap | |
| Highlight current line | |
| Enable scrolling beyond last line | |
| Enable multi-editing (multi-selection) | required for the Multi-select commands |
| Caret width | 1–3 |
| Vertical edge at column | 0–300; **0 = off** |
| Caret blink rate (ms) | 0–2000; **0 = steady** |
| Use a custom line-number margin colour | off by default — the gutter follows the theme. When on, the colour picker beside it becomes active and takes effect immediately, no restart |

## Indentation

| Setting | Notes |
| --- | --- |
| Tab size | 1–16 |
| Replace by space | insert spaces instead of tab characters |
| Auto-indent new lines | |

## Auto-Completion

| Setting | Notes |
| --- | --- |
| Enable auto-completion on each input | |
| From the *N*th character | 1–10 — how many characters must be typed before suggestions appear |
| Auto-insert matching brackets and quotes | |

## New Document

Defaults applied to newly created documents:

- **Format (Line ending)** — Windows (CR LF), Unix (LF) or Macintosh (CR)
- **Encoding** — UTF-8, UTF-8 with BOM, UTF-16 LE, UTF-16 BE or ANSI
- **Default language** — *Normal Text* or any built-in language

## Tab Bar

- **Show close button on each tab** — *restart*

## Recent Files History

- **Max number of entries** — 1–50, *restart*

## Print

- **Header** and **Footer** — leave blank for none.
- Available macros:

```
$(FULL_PATH)   $(FILE_NAME)   $(DATE)   $(TIME)
$(CURRENT_PRINTING_PAGE)   $(TOTAL_PRINTING_PAGES)
```

The two page macros are resolved per page as the document prints; the rest are resolved once.

---

## What needs a restart

Clicking **OK** prompts to restart if any of these changed: **Theme**, **Localization**, **Toolbar
icon style**, **Toolbar icon size**, **Reuse an existing window**, **Show close button on each tab**,
**Max number of entries** (recent files), **Show integrated top bar**, or **System-native window
buttons**. Everything else applies immediately.

If a restart is offered while documents have unsaved changes, the save prompt runs first — the new
values are only written once the restart is actually confirmed, so cancelling out leaves the old
settings intact.

## Where settings are stored

Settings go through `wxConfig` under the application name **wxNote** — the registry on Windows, a
config file under the user's config directory elsewhere. User-writable data (recovery backups,
user-defined languages, `contextMenu.xml`, `shortcuts.json`) lives in the per-user data directory,
**not** next to the executable, so installed builds work without write access to their install
directory. Keyboard-shortcut overrides are the one exception to the `wxConfig` rule — they persist in
`shortcuts.json`; see [Customizing Shortcuts](custom-shortcuts.md).
