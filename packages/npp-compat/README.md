# npp-compat — the optional Notepad++ compatibility layer

`npp-compat` brings **a user's Notepad++ setup into wxNote**. It reads Notepad++'s own files —
`config.xml`, `shortcuts.xml`, themes (`stylers.xml`, `themes/*.xml`), `contextMenu.xml`, `langs.xml`,
sessions and Project-panel workspaces — and translates each into what wxNote keeps for itself, which is plain YAML
(see [`docs/SETTINGS_DESIGN.md`](../../docs/SETTINGS_DESIGN.md)). wxNote's core reads none of these
formats: everything Notepad++-shaped happens here.

## Why it's a separate, GPL module

This module reproduces **Notepad++'s file formats** — the five sections of `shortcuts.xml` and its
Windows-VK decimal key encoding, `config.xml`'s `GUIConfig` entries and language numbers, the
`<NotepadPlus>` theme, context-menu, session and project schemas — in order to interoperate with them.
That is exactly the kind of work wxNote confines to optional, separately licensed modules, so like
[`udl-compat`](../udl-compat/README.md) and [`npp-bridge`](../npp-bridge/README.md) this one is
**GPL-3.0-or-later**. The wxNote **core depends on none of it** and is loaded without it; delete the
plugin and you lose only the import. Keeping the format reproduction here is what lets the core stay
Apache-2.0 (see [`LICENSING.md`](../../LICENSING.md)). For the same reason, the table of Notepad++'s
English menu names that a `contextMenu.xml` can name its items by (`npp_menu_names.h`, from Notepad++'s
own `localization/english.xml`) lives here and nowhere else.

The translations write wxNote's files with the core's own wx-free headers — `src/yaml_io.h`,
`src/theme_file.h`, `src/settings_schema.h` (Apache-2.0, which GPL code may use) — and rapidyaml, so
what this module writes is exactly what wxNote reads.

## What each file becomes

| Notepad++ file | Becomes | How |
| --- | --- | --- |
| `shortcuts.xml` | a **"Notepad++ (imported)"** key-binding scheme | `nib.keymap/1`: `InternalCommands` → `bind_id` (Notepad++'s `IDM_*` numbers are wxNote's command numbers), `ScintillaKeys` → `bind_editor`, `PluginCommands` → `bind_name` (`npp.<module>.<internalID>`) |
| `config.xml` | values in `settings.yaml` — tab size and tabs or spaces, word wrap, line numbers, whitespace, caret, auto-completion, new-document EOL / encoding / language, dark mode, the theme, recent-files count, … | `nib.settings/1`, each value checked by the host against its table of settings; the theme becomes the imported copy, else wxNote's theme of that name, and is refused when wxNote has neither |
| `stylers.xml`, `themes/*.xml` | `themes/<name> (Notepad++).yaml` (`stylers.xml`: *Notepad++ (imported)*) in the user's theme folder | written directly; the theme's credits comment becomes its header |
| `contextMenu.xml` | `contextmenu.yaml` | items by command number (shared with Notepad++) or by menu name (`MenuEntryName`/`MenuItemName`, looked up in Notepad++'s English menu names, `npp_menu_names.h`, as Notepad++ looks them up); `FolderName` submenus become `{menu, items}` (with wxNote's names for the stock ones that carry a `TranslateID`), `ItemNameAs` becomes `{command, label}`, and a plugin's command (`PluginEntryName`/`PluginCommandItemName`) becomes `{plugin, command}`, which wxNote shows when that plugin is loaded |
| `langs.xml`, and the active theme's *User-defined keywords* | entries laid over the user's `languages.yaml` | only what the user added: `langs.xml` is compared with Notepad++'s own `langs.model.xml` (beside it, or in an installed Notepad++'s program folder) - without one only the theme's keywords come across, and a language the model lacks (another Notepad++ version's) is reported, not imported. Added extensions and keywords, changed comment tokens; Notepad++'s keyword classes go to wxNote's lists as its lexers wire them (by number, the C family's `type1` to *types* and `instre2` to *globalClasses*, the HTML family's markup and script lists shared by HTML, PHP, ASP and JSP, `substyleN` to the user keyword groups) |
| a session `.xml` | its files, opened, with the active file's position and bookmarks | `nib.session`, through a scratch wxNote session (`nib.documents` on a host without it) |
| a workspace `.xml` | a `.yaml` workspace beside it, ready for a Project panel | written directly |

Whatever has nowhere to go — a Notepad++ preference wxNote has no equivalent for, a context-menu item
named by text no Notepad++ menu has, a key with no portable spelling — is listed in the report, not
dropped silently. A file an import replaces (your `contextmenu.yaml`, your `languages.yaml` - which the
import lays its entries over, keeping the rest - a theme you edited after an earlier import, a translated
workspace) is kept aside first as `.bak` — or `.bak2`, `.bak3`… — never over an earlier backup; an import
that would write the same thing again changes nothing.

## How to use it

The plugin adds three commands to the **Extensions** menu:

- **Import from Notepad++…** reads a Notepad++ settings folder — an installed Notepad++'s
  `%APPDATA%\Notepad++` on Windows, or a copy of that folder put in `<wxNote user data>/notepad++` on
  any system — and imports everything above that it finds there, then opens one report.
- **Import the Open Notepad++ File** does the same for the one file in front, whatever kind it is
  (the kind is detected from its content). This is how a session or a workspace comes in.
- **Import Notepad++ shortcuts.xml…** (well-known id `host.shortcuts.import`) — the key bindings
  alone, with a detailed validation report. The core's **Run ▸ Validate shortcuts.xml** forwards to
  it. It looks in wxNote's user data folder first, then in the folders above.

The imported key-binding scheme is not switched to — pick it in the Shortcut Mapper. It is stored in
wxNote's `keybindings.yaml` and **outlives the plugin**; so do the themes, settings and menu it writes.
If wxNote cannot keep the scheme (its `keybindings.yaml` does not parse, or a newer wxNote wrote it),
the import says so instead of reporting a success.

The same translations run without wxNote, from a terminal:

```sh
cmake --build build --target npp2wxnote
build/bin/npp2wxnote theme       "Notepad++/themes/Zenburn.xml" Zenburn.yaml
build/bin/npp2wxnote config      "Notepad++/config.xml"           # prints the settings.yaml lines
build/bin/npp2wxnote contextmenu "Notepad++/contextMenu.xml" contextmenu.yaml
build/bin/npp2wxnote languages   "Notepad++/langs.xml" languages.yaml langs.model.xml "Notepad++/stylers.xml"
build/bin/npp2wxnote workspace   project.xml project.yaml
build/bin/npp2wxnote session     session.xml                      # prints the files it lists
build/bin/npp2wxnote detect      some.xml                         # what kind of Notepad++ file it is
```

## Security stance

- **`UserDefinedCommands` in `shortcuts.xml` carry shell command lines** (e.g.
  `firefox $(FULL_CURRENT_PATH)`). They are **pure data** here: parsed and listed in the report *for
  review only, and never executed*. There is no `ShellExecute` / `system` / `CreateProcess` /
  `wxExecute` anywhere in the package. Macros are likewise parsed and counted but never replayed.
- **Every Notepad++ file is only read**; none is ever written back.
- **The XML reader is hardened** (`npp_xml.{h,cpp}`): no DTD or external-entity expansion — only the
  five predefined entities and numeric character references are decoded — a nesting limit of 64 and
  an element limit of two million, and every untrusted number clamped to a sane range.
- Everything wxNote's files receive goes through the same writers wxNote itself uses, and every
  setting through the host's own validation.

## Building and testing

`npp-compat` builds as part of wxNote (the top-level `add_subdirectory(packages/npp-compat)`), since it
uses the core's headers and the fetched rapidyaml:

| Target | What |
| --- | --- |
| `npp_compat_plugin` | the runtime plugin, `bin/nib/npp_compat.dll` (`.so` / `.dylib`) |
| `npp_compat_selftest` | the parser and translation tests, run by `ctest` |
| `npp2wxnote` | the converter above (on request) |
| `npp2accel` | `shortcuts.xml` → accelerator strings, for inspecting a file (on request) |

The self-test covers the XML reader (entities, `DOCTYPE`, comments, the limits), all five
`shortcuts.xml` sections, the VK→token table and accelerator builder, the "command line captured as
data, never executed" case, the report format, and each translation — theme, `config.xml`, context
menu, session and workspace — with the YAML it writes read back by wxNote's own reader.

## Files

| File | Role |
| --- | --- |
| `npp_xml.{h,cpp}` | the hardened, iterative XML reader every translation uses |
| `npp_shortcuts_parse.{h,cpp}` | `shortcuts.xml` → data model + the validation report |
| `npp_shortcuts_accel.{h,cpp}` | Windows VK → wx accelerator token + accelerator-string builder |
| `npp_translate.{h,cpp}` | file-kind detection and the theme, `config.xml`, context-menu and workspace translations |
| `npp_session.{h,cpp}` | Notepad++ session XML, read and written — also what `npp-bridge` answers plugins' session messages with |
| `npp_compat_plugin.cpp` | the Nib plugin: finds the files, hands the results to the host, writes the report |
| `npp2wxnote.cpp`, `npp2accel.cpp` | the command-line tools |
| `selftest.cpp` | the tests |

This package is deliberately the one place that knows both Notepad++'s files and wxNote's, and is
scoped to eventually move to its own repository.
