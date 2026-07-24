# Plugins Admin — design (deferred)

**Status: designed, intentionally NOT built.** An in-app plugin browser/installer only pays off once a
third-party plugin *ecosystem* exists, which wxNote does not have yet. Spell-check and File-Compare serve
every user on day one; a plugin store serves nobody until there are plugins to browse. This document
records the right-sized design so it can be picked up when the ecosystem justifies it. Until then,
**Extensions ▸ Open Plugins Folder…** stays as-is.

## What it is (and is deliberately NOT)

The goal is small: let a user *see available plugins and click install*. That is a **catalog** — a file —
not a marketplace. wxNote should **not** stand up or self-host a marketplace platform.

This was checked exhaustively. Every reuse candidate was evaluated and rejected as either the wrong size
or the wrong shape for a native, single-binary, solo-maintained editor:

| Considered | Why not |
| --- | --- |
| **NuGet / Maven / Gradle** | Clients need a .NET/JVM runtime (non-starter for a native single binary, esp. riscv64); public registries have the wrong semantics + publishing friction and model no curation. Gradle isn't even a repo format. |
| **Conan** | `package_id` over-forks the matrix on compiler/build_type; Python client is the same runtime objection; ConanCenter is for libraries, not app plugins. (Legit only for wxNote's *own* build deps — a separate CI question.) |
| **CPM.cmake** | Build-time source fetch (FetchContent wrapper). No registry, no binaries, no runtime install. Category error. |
| **SCons** | A build *system*, not a distribution channel. Category error. |
| **Eclipse Open VSX** | The one real off-the-shelf contender (EPL-2.0 server, self-hostable, public REST API, and it *can* host per-`targetPlatform` `.vsix` with native binaries inside). But it's a **lateral move**: JVM + Elasticsearch + PostgreSQL to operate forever, mandatory VS Code manifest ceremony, and **no riscv64 target slot**. Buys a web UI + publisher accounts wxNote doesn't need. |
| **Mozilla addons-server** | BSD-3 (great license) but a Django monolith built for Firefox WebExtensions; no native per-`(os,arch)` binary concept. Heaviest of all. |
| **AppStream / Flathub** | Whole-app metadata, Linux-only. Not a cross-platform plugin store. |

**Conclusion:** the convergent pattern every end-user-plugin ecosystem uses (Obsidian, Sublime Package
Control, JetBrains, Notepad++/nextpad) — a **bespoke JSON catalog + per-plugin GitHub Releases** — is the
correct, right-sized, already-permissive answer. It is a file you own, with no service to run and no
copyleft. Design DNA worth borrowing: Open VSX's REST API shape + its `targetPlatform` naming, and
AppStream's MetaInfo metadata shape (incl. its mandatory SPDX license field) — as *references*, not backends.

## The design (when built)

### Catalog
- **Fork the nextpad-plus-plus per-`(os,arch)` extension of the `nppPluginList` schema**: a small top-level
  `index.json` + one `pl.<os>-<arch>.json` per target, hosted as static files (GitHub Pages / any CDN).
- Add a **`kind`** field (`"nib"` | `"npp-bridge"`) that routes the install path — `nib` → the per-user
  Nib dir, `npp-bridge` → the plugins dir — and lets the UI grey out N++ plugins off Windows / when the GPL
  bridge isn't installed.
- Each entry carries human-facing metadata (display name, description, author, homepage, **license badge**),
  a stable reverse-DNS `id`, `version`, `min/max-host-abi` (gated on `NIB_ABI_VERSION`), and an
  `install` block: `folder-name`, `binary`, `package` URL (a GitHub release asset), `sha256`, `size`.
- Binaries live as each plugin's own **GitHub release assets**; the catalog stores only URLs + hashes.

### Client (in-app)
- `wxWebRequest` (async HTTPS, already in wx 3.3.1 — no new dependency) fetches `index.json` → the matching
  per-arch list → on install, the `package` ZIP.
- Pipeline: download → **verify SHA-256** → verify signature (below) → `wxZipInputStream` extract →
  per-user plugins dir → mark restart-to-apply. A `wxDialog` with Available/Updates/Installed tabs.
- **Prerequisite (Phase 0):** teach the loader to also scan a **per-user** plugins root under
  `userDataDir()` (installed builds can't write under Program Files — the `user-data-dir-not-exedir`
  gotcha), and write an `installed.json` manifest for update detection.

### Trust — the one upgrade worth taking from the whole survey
Releases aren't code-signed yet, so a hash inside an unauthenticated JSON is anti-corruption, not
anti-tamper. Add a **detached Ed25519 signature sidecar** (`.minisig` / `.asc`) beside each artifact and the
catalog, verified against a **public key compiled into wxNote**. Primitive: **libsodium** (ISC) or
**minisign/signify** (public-domain-ish) — a small native verify, ~200 LOC, no PKI, no cost, cross-platform
incl. riscv64. This upgrades trust from "trust the host" to "trust the key." (TUF via AWS `tough` solves
key-rotation/rollback too, but there is no C/C++ TUF client — reimplementing it is disproportionate for a
solo-curated catalog. Defer TUF; ship Ed25519.)

### Curation
The **PR into the catalog is the trust event** — the one thing no package registry provides and the actual
product surface. MVP = maintainer-only allowlist; open to third-party PRs later with schema + hash-reachability
CI. Surface a "plugins are community-contributed; the wxNote team curates the list but does not audit plugin
code" notice.

## Phased plan (when justified)
- **Phase 0 — loader groundwork** (~2–3 days): per-user plugins root scan + `installed.json` + vendor a SHA-256.
- **Phase 1 — MVP install-from-catalog** (~1–1.5 wks): menu entry + `wxWebRequest` fetch → SHA-256 verify →
  extract → per-user dir → restart. Hand-maintained catalog repo + JSON-schema CI, seeded with the reference
  plugins.
- **Phase 2 — updates + trust** (~1 wk): Updates tab (version compare), scheduled uninstall, kind/arch/ABI
  grey-out, license badges, **minisign the catalog + verify in-app**.

Total to shippable: ~3–4 weeks — the bulk in the download/verify/extract pipeline and the catalog tooling,
not the dialog. New UI strings must go through `_()` into `wxn.pot` (the standing i18n gap).

## Pointers for whoever picks this up
- Menu/command: `src/command_ids.h` (Extensions range, next to `kCmdSettingOpenPluginsDir`), `src/menu_data_plugins.h`, `src/menu_labels_plugins.h`.
- Install-path ladder to reuse: `src/main.cpp` around `kCmdSettingOpenPluginsDir` (`<exe>/plugins` vs `userDataDir()/plugins` vs `<exe>/nib`).
- ABI gate: `include/nib/nib.h` (`NIB_ABI_VERSION`). Signing posture to mirror: `docs/SIGNING.md`.
- Tracking: `docs/MISSING_FUNCTIONALITY.md` (Plugins Admin row), user docs `site/docs/plugins.md` / `site/docs/menus.md`.
