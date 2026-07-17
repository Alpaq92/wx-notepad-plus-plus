# Goals

Why wxNote exists, what it is trying to be, and the three decisions that
shaped it.

## Where the project comes from

The spark was **Nextpad++** — a project that brought Notepad++ to macOS, and
*only* macOS. It proved there was real appetite for the Notepad++ experience
beyond Windows, and it was a pity it didn't support Linux: every platform
seemed to get, at best, its own isolated one-platform answer.

What was then called *wxNotepad++* initially aimed to be exactly that kind of
answer — a port of Notepad++ itself to the platforms it never reached. The
repository's own history reflects it: the project literally began by putting
Notepad++'s real source under a modern CMake build and getting it compiling
and running. But the review that followed made the conclusion obvious:
**porting Notepad++ would cost so much effort that writing a new editor from
scratch was no more expensive** (see "Why porting Notepad++ is so hard"
below).

So the scope changed. Instead of dragging Notepad++'s code across platforms,
the project became **wxNote**: an editor *inspired by* Notepad++, giving
users the same vibe — the same feature set, defaults, and workflow — without
using its code. That decision is also what made the licensing freedom
possible: a from-scratch editor owes no GPL inheritance, which is why
wxNote's core ships under the permissive Apache-2.0 license instead of
Notepad++'s GPL (see [`LICENSING.md`](../LICENSING.md)).

## Why porting Notepad++ is so hard

Notepad++ is not a cross-platform program with a Windows build — it is a
**Win32 program**, welded to Windows at every layer:

- The entire UI is raw Win32: every dialog is a Windows resource-script
  dialog, the docking system, tab bar, and toolbar are custom Win32/GDI
  code, and behavior is wired through the Windows message loop
  (`WM_COMMAND`/`WM_NOTIFY` plumbing throughout).
- The **plugin ABI is Win32 by definition** — plugins are DLLs that receive
  real window handles and communicate via Windows messages. There is no
  portable seam to reimplement behind.
- It leans on Windows-only services throughout: the registry, shell
  integration, `SendMessage`-based IPC, Windows-specific file semantics.
- The only genuinely portable pieces are the ones Notepad++ itself borrowed:
  **Scintilla** (editing) and **Lexilla** (highlighting). wxNote adds one
  more portable, permissively-licensed engine of its own for custom
  languages — **Scintillua** (Lua + LPeg lexer grammars, MIT) — in place of
  Notepad++'s Win32-era User-Defined-Language subsystem.

A faithful port therefore means rewriting essentially every UI and platform
file while keeping behavior identical — which *is* writing a new editor,
except harder: you inherit two decades of Win32-shaped architecture *and*
the GPL, without gaining portability-oriented design anywhere. Once that was
clear, a clean-room rewrite that keeps only the portable engine (Scintilla +
Lexilla) and the *behavior* was the strictly better deal.

## Why wxWidgets

wxWidgets was the natural toolkit for that rewrite:

- **Native controls on every platform.** wxWidgets translates to the
  platform's own widgets — Win32 on Windows, GTK on Linux, Cocoa on macOS —
  rather than drawing imitations of them. A wxNote menu, dialog, or file
  picker is the real native one, which is exactly the "feels at home
  everywhere" quality a Notepad++-class desktop editor needs.
- **It speaks Scintilla natively.** `wxStyledTextCtrl` embeds Scintilla —
  the very editing engine Notepad++ uses — so the core editing feel carries
  over through a portable wrapper, and the vendored Lexilla supplies the
  same syntax highlighting.
- **Same language, mature everything-else.** It's C++ like the engine it
  wraps, and it brings the supporting machinery a full editor needs out of
  the box: AUI docking for the panels, printing, i18n, configuration,
  single-instance IPC.
- **License-compatible with the goal.** The wxWindows Licence (LGPL with a
  static-linking exception) puts no copyleft pressure on wxNote's
  permissive core.

## Platforms

The whole point of the project is reaching the users Notepad++ (and its
single-platform derivatives) couldn't — so wxNote puts deliberate, ongoing
effort into being installable by as many people as possible, in whatever
form their system expects. That's why one release carries **thirteen
packages**:

- **Windows** — an NSIS installer, built separately for **x64** and
  **ARM64** (Windows-on-ARM laptops get a native binary, not x64
  emulation).
- **Linux** — no single format reaches all of Linux, so every release ships
  **four**: an **AppImage** (runs anywhere, no install), a **.deb**
  (Debian/Ubuntu/Mint), an **.rpm** (Fedora/openSUSE), and a **.flatpak**
  (distro-agnostic sandboxed install) — each built for **x86_64 and
  aarch64**, covering ARM laptops, Raspberry Pi-class boards, and ARM
  servers; the **.deb** additionally ships a **riscv64** build for RISC-V
  hardware (the other three formats have no riscv64 runtime yet).
- **macOS** — a `.dmg` built separately for **Apple Silicon** and
  **Intel**, so each download is small and native rather than a fat binary.

Every one of these is built by CI from the same single codebase on every
release — no platform is a hand-built afterthought. The same
reach-everyone intent drives the UI localization: wxNote ships fully
translated into eight languages (Polish, German, French, Spanish, Russian,
Japanese, Chinese, Korean), with English as the source text.

## Signing

Every release now publishes a **`SHA256SUMS`** covering every artifact, so a
download can be integrity-checked today. Beyond that, **wxNote's releases are
not yet code-signed** — closing that gap is one of the project's priorities.
The signing pipeline is already wired and gated on repository secrets (GPG
signature of the checksums, Windows Authenticode, macOS notarization); each
activates automatically once its certificate/key is configured, with no code
change. See [`docs/SIGNING.md`](SIGNING.md).

In practice, unsigned currently means:

- **Windows** — the installer carries no Authenticode signature, so
  SmartScreen shows the "unknown publisher" warning on first run
  (More info → Run anyway).
- **macOS** — the app is neither Developer-ID-signed nor notarized, so
  Gatekeeper blocks the first launch until you right-click → Open (or allow
  it under System Settings → Privacy & Security).
- **Linux** — the packages and AppImage ship without GPG signatures;
  integrity currently rests on downloading from the official GitHub
  Releases page (which is also the only place wxNote is published — treat
  anything found elsewhere with suspicion).

The gap is infrastructural, not philosophical: signing requires paid
certificates, an Apple Developer membership, and identity/key management
that a young, non-commercial project hasn't stood up yet. Getting there —
an Authenticode certificate for the Windows installers, Apple notarization
for the `.dmg`s, and published signatures/checksums for the Linux
artifacts — is high on the roadmap precisely because of the availability
goal above: a scary first-run warning is a real barrier for exactly the
non-expert users the multi-platform effort is meant to reach.

## Licensing

wxNote has **no ambition of being monetized** — it is not for sale, carries
no paid tier, and is not run with any commercial exploitation in mind. The
project exists to be used, not to earn.

Given that, why relicense from GPL to Apache-2.0 at all? **Solely to give
users and the community more freedom.** A permissive license means anyone
can use, study, embed, fork, and redistribute wxNote with the fewest
possible strings attached — including in places copyleft makes awkward,
like other permissively-licensed projects. The relicense changed what
*others* are allowed to do with the code; it changed nothing about what the
project itself is: free, open source, and non-commercial in spirit either
way. The exceptions in what ships are the two optional interoperability
modules — the Notepad++ plugin bridge (`packages/npp-bridge/`, plus its
never-shipped test fixture) and the Notepad++ UDL compatibility plugin
(`packages/udl-compat/`) — which stay GPL because of the Notepad++ formats
and ABIs they reproduce; see "The plugin system" below and
[`LICENSING.md`](../LICENSING.md) for the precise per-component record.

## The plugin system

Plugins are first-class, and they are two-tier by design:

- **Nib** (`include/nib/nib.h`) is wxNote's own plugin API — an original,
  cross-platform, stable C ABI. Nib plugins are ordinary shared libraries
  and work on all three platforms. This is the project's real extension
  surface, and it is Apache-2.0 like the rest of the core.
- **npp-bridge** (`packages/npp-bridge/`) is a **non-obligatory extension**:
  an optional, cross-platform plugin (itself just a Nib plugin) that hosts
  compiled Notepad++ plugin binaries by reproducing Notepad++'s plugin ABI
  and translating it to Nib calls. On Windows it `LoadLibrary`s real,
  compiled Notepad++ plugin *DLLs*; on Linux/macOS it `dlopen`s Notepad++
  plugins recompiled against the bundled `libnpp_shim` (the Win32-only
  docking/window-message machinery is `#ifdef`'d out off-Windows — see
  [`docs/ARCHITECTURE.md`](ARCHITECTURE.md)). Because it is a Notepad++-ABI
  derivative, it is **GPL** — the core never depends on it, ships and runs
  fine without it, and the module is slated to move into its own repository.
  Delete the bridge module (`npp_bridge.dll` on Windows,
  `npp_bridge.so`/`.dylib` elsewhere) and nothing else changes. (A
  never-shipped, Windows-only dev test fixture, `packages/test_plugin/`,
  tracks the same GPL — see [`LICENSING.md`](../LICENSING.md).)
- **udl-compat** (`packages/udl-compat/`) is the other optional GPL Nib
  plugin. wxNote's native custom-language engine is **Scintillua** (Lua/LPeg
  lexer grammars, MIT), embedded in the Apache core; udl-compat is the
  separate, cross-platform compatibility path that reads a user's legacy
  Notepad++ `userDefineLang.xml` files, translates each into a Scintillua
  lexer, and registers it via the new `nib.langdef` API. Because it is the
  one place that reproduces Notepad++'s UDL format, it is **GPL-3.0-or-later**
  and lives outside the Apache core; like npp-bridge it is optional and
  scoped to move to its own repository (see
  [`docs/ARCHITECTURE.md`](ARCHITECTURE.md)).

That split is the licensing strategy in miniature: everything original is
permissive; the deliberately Notepad++-shaped pieces are isolated,
optional, and honestly labeled.

## The goals, in short

- The Notepad++ experience — features, defaults, vibe — on **Windows, Linux,
  and macOS as equals**, on x64 and ARM alike, not one platform plus
  afterthoughts.
- **No Notepad++ code.** Behavior compatibility (file formats, command ids,
  plugin loading) comes from clean-room reimplementation, never from copying.
  Even the menus are wxNote's own: the item ordering follows its own
  frequency/affinity scheme, and only the frozen command ids and shared
  command labels coincide with Notepad++.
- **Permissive by default.** Apache-2.0 core; in everything that ships, GPL
  is confined to the optional interoperability modules (the Notepad++
  plugin bridge and the UDL-compatibility plugin).
- **Plugins everywhere.** A first-class, cross-platform plugin API, with
  legacy Notepad++ plugin support as an optional, cross-platform bridge
  (real compiled plugin binaries on Windows; recompiled plugins loaded via
  `dlopen` on Linux/macOS) rather than an architectural constraint.
