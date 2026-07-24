# Bundled editor fonts — attribution

Five monospace families are bundled. **Cascadia Mono is the default**; JetBrains Mono, IBM Plex Mono,
Hack and Iosevka Fixed are the additional bundled choices, all pinned in the font picker. Cascadia Mono,
JetBrains Mono, IBM Plex Mono and Iosevka Fixed are under the
[SIL Open Font License, Version 1.1](https://openfontlicense.org); Hack is under the MIT License plus the
Bitstream Vera License (both permissive).

| Family | Copyright | Reserved Font Name | Files | License text |
|---|---|---|---|---|
| **Cascadia Mono** | © 2019 – Present, Microsoft Corporation | **yes** — `Cascadia Code` | `CascadiaMono-Regular.ttf`, `CascadiaMono-Bold.ttf` | `CascadiaMono-OFL.txt` |
| **JetBrains Mono** | © the JetBrains Mono Project Authors | **none** | `JetBrainsMono-Regular.ttf`, `JetBrainsMono-Bold.ttf` | `JetBrainsMono-OFL.txt` |
| **IBM Plex Mono** | © 2017 IBM Corp. | **yes** — `Plex` | `IBMPlexMono-Regular.ttf`, `IBMPlexMono-Bold.ttf` | `IBMPlexMono-OFL.txt` |
| **Hack** | © 2018 Source Foundry Authors; Bitstream Vera © 2003 Bitstream, Inc. | **none** (MIT + Bitstream Vera) | `Hack-Regular.ttf`, `Hack-Bold.ttf` | `Hack-LICENSE.md` |
| **Iosevka Fixed** | © 2015 – 2026 Renzhi Li (Belleve Invis) | **yes** — `Iosevka` | `IosevkaFixed-Regular.ttf`, `IosevkaFixed-Bold.ttf` | `IosevkaFixed-OFL.txt` |

- Cascadia Mono — <https://github.com/microsoft/cascadia-code>
- JetBrains Mono — <https://github.com/JetBrains/JetBrainsMono>
- IBM Plex Mono — <https://github.com/IBM/plex>
- Hack — <https://github.com/source-foundry/Hack>
- Iosevka Fixed — <https://github.com/be5invis/Iosevka> (the "Fixed", no-ligature build)

Only the Regular and Bold weights of each are bundled, **unmodified from upstream**.

## Why a font is bundled at all

The editor's `STYLE_DEFAULT` font (`src/main.cpp`) is a bundled face rather than Consolas: Consolas is
a proprietary Microsoft font that happens to already be installed on Windows but isn't ours to
redistribute, and isn't present on Linux/macOS at all — naming it there would silently fall back to an
arbitrary system font. Bundling means every platform gets the same, permissively-licensed,
code-oriented monospace out of the box.

## Reserved Font Name — the one difference that matters

JetBrains Mono's OFL notice declares **no** Reserved Font Name. Cascadia's does:

> Copyright (c) 2019 - Present, Microsoft Corporation,
> with Reserved Font Name Cascadia Code.

Under OFL §3 a Reserved Font Name may not be used on a *modified* version. Practically, for this
repository: the Cascadia files may be redistributed as they are (that is all we do), but they must not
be renamed, subsetted, re-hinted, patched, or otherwise altered while still carrying the reserved name.
If a modified cut is ever needed it has to be renamed first. JetBrains Mono carries no such
restriction.

## Why Cascadia **Mono** and not Cascadia **Code**

Mono is the ligature-free cut. Scintilla draws through GDI on Windows (nothing in `src/` calls
`SCI_SETTECHNOLOGY`), and GDI does no OpenType shaping, so Code's programming ligatures would never
render anyway — Mono is the smaller, honest choice.

Regular and Bold both report GDI family name (name ID 1) `Cascadia Mono` with subfamilies
`Regular`/`Bold`, so they pair as one family and bold syntax styles get the **real** Bold face instead
of a GDI-synthesised one. That matters for a monospace font: synthesised bold measures roughly 11%
wider and pulls bold tokens out of column alignment — the trap a separate-family weight cut such as
SemiBold falls into.

## How they're loaded

`wxFont::AddPrivateFont()` in `WxnApp::OnInit` (`src/main.cpp`) registers all ten files for the
current process only — on Windows, on Linux (via fontconfig) and on macOS (via CoreText). No
installation, no admin rights, and nothing is added to the user's system-wide font list; the
registrations die with the process, so `OnExit` has no teardown to do.

Both families are listed explicitly at the top of the **Preferences ▸ Editing ▸ Font** picker, above a
divider and the enumerated system fonts. If the configured face is empty or no longer installed,
`effectiveFontFace()` falls back to **Cascadia Mono**.

> Historical note: font registration previously used a raw `::AddFontResourceExW` guarded by
> `#ifdef __WXMSW__`, so the font was shipped inside every Linux/macOS package but **never actually
> registered** there — those platforms silently fell back to their system default monospace, meaning
> the deliberately chosen default was not in effect on two of the three platforms. The cross-platform
> call fixed that.

See the root `LICENSING.md` and `NOTICE` for the project-wide licensing summary this entry is
consistent with.
