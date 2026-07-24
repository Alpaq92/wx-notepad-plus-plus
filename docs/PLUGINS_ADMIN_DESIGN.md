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

## Prior art surveyed: Pulsar, VS Code, Sublime (2026-07)

Three editors were studied as the three distinct archetypes of editor plugin distribution. None targets
wxNote's actual problem (native per-`(os,arch)` binaries incl. riscv64, from a solo maintainer with no
server budget), and each fails in an instructive way — together they argue *for* the bespoke design below.

| Axis | **Pulsar** (Atom fork) | **VS Code** (MS / Open VSX) | **Sublime** (Package Control) |
| --- | --- | --- | --- |
| Catalog | Live self-hosted registry: Node + Express + **PostgreSQL** + Google Cloud Storage on App Engine | Live marketplace: MS proprietary / Open VSX = **Spring Boot + PostgreSQL + Elasticsearch + React** | Curated-PR git repo **+ a live crawler** → one compiled `channel_v3.json` (~5 TB/mo) |
| Who runs it forever | Volunteer team, **donation-funded** | Microsoft / Eclipse Foundation | 1 person for ~a decade → **handed to a company (2026)** |
| A "package" is | npm `package.json` in a public GitHub repo | `.vsix` (ZIP/OPC) of **JS/TS** + manifest | git-tag repo or `.sublime-package` zip; **pure Python** |
| Native per-`(os,arch)` | ✗ **compile-on-install** (node-gyp vs Electron headers) | `targetPlatform` enum — **no `riscv64` slot** | ✗ (interpreted) |
| Signing | ✗ none (TLS + TOFU + curation) | MS signs & verifies each VSIX; Open VSX weaker | ✗ none (TLS + TOFU + curation) |
| Cautionary tale | atom.io's death *is why Pulsar exists* | MS ToS legally bars non-MS clients from the Marketplace | **2021 CVE**: re-registered username + moved tag shipped as an "upgrade" |
| License | MIT | proprietary / EPL-2.0 | MIT |

**What each teaches:**
- **Pulsar = the ops-burden cautionary tale.** Resurrecting atom.io meant a live DB + API + cloud storage
  that must run forever (donation-funded, single point of failure); its native story is *compile on the
  user's machine*, unworkable for a no-runtime app and with no better story for riscv64. → Against any
  always-on server and against compile-on-install; for "registry = thin metadata pointing at GitHub."
- **VS Code = the conventions reference you can't actually use.** `targetPlatform` (one version, many
  per-`(os,arch)` artifacts) is the right *shape*, but the enum has **no riscv64**, so an existing
  marketplace physically cannot represent wxNote's target; MS's ToS bars non-MS clients; Open VSX is a
  heavy JVM stack under EPL-2.0. Worth stealing: **sign every artifact and verify on install.**
- **Sublime = the closest kin and the clearest security lesson.** Curated JSON entries by PR, bytes
  offloaded to GitHub, a single index blob the client GETs — almost exactly this design. But it needs a
  live crawler resolving *mutable git tags* and has *no signing*, and that combination is what the 2021
  CVE exploited. → Borrow curated-PR + single-static-index + index-points-at-bytes; drop the crawler and
  the mutable-tag trust.

**Convergence:** all three point back at the bespoke **static JSON catalog + per-plugin GitHub-Release
assets + compiled-in-key Ed25519 signing + curated PRs** — the only shape that satisfies native-per-arch,
riscv64, no-server, and permissive-license at once. The one sharpening the survey forces: **pull signing
forward to Phase 1** (see Trust).

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

**Threat model (why signing is not optional — Sublime, Feb 2021).** Package Control shipped for years on
TLS + trust-on-first-use + curation and *no artifact signing*. An attacker deleted a package's GitHub/
BitBucket account, re-registered the same username, recreated the repo, and pushed a higher version tag —
which the crawler distributed to every user as a legitimate "upgrade" (32 accounts found vulnerable;
reported by Apple Information Security). The fix was purely operational (offline sources now need manual
re-review), not cryptographic. That is exactly the attack a hash-in-a-JSON does **not** stop but a
signature does: a re-registered account cannot produce a signature valid under the maintainer's compiled-in
key. Three consequences for wxNote:
- **Sign in Phase 1, not Phase 2.** The signature is the one trust gate a compromised host or a
  re-registered account cannot forge; it is ~200 LOC and must not ship after an unsigned install window.
- **Pin immutable artifacts.** Store the **Release-asset URL + `sha256` + `size`**, never a "latest tag"
  on a third-party account — closing the moved-/re-registered-tag class before the signature check even runs.
- **MVP trust = a single maintainer key** (you curate → you sign). Per-publisher keys can come later; do
  not build a PKI for a solo-curated catalog.

### Curation
The **PR into the catalog is the trust event** — the one thing no package registry provides and the actual
product surface. MVP = maintainer-only allowlist; open to third-party PRs later with schema + hash-reachability
CI. Surface a "plugins are community-contributed; the wxNote team curates the list but does not audit plugin
code" notice.

## Phased plan (when justified)
- **Phase 0 — loader groundwork** (~2–3 days): per-user plugins root scan + `installed.json` + vendor a SHA-256.
- **Phase 1 — MVP install-from-catalog, signed** (~1.5–2 wks): menu entry + `wxWebRequest` fetch → SHA-256
  verify → **Ed25519 signature verify against the compiled-in key** → extract → per-user dir → restart.
  Hand-maintained catalog repo (immutable Release-asset URL + `sha256` + `size` per entry) + JSON-schema CI,
  seeded with the reference plugins. Signing is *in* the MVP — see Trust's threat model, moved up from Phase 2.
- **Phase 2 — updates + polish** (~1 wk): Updates tab (version compare), scheduled uninstall, kind/arch/ABI
  grey-out, license badges, and a documented (even if manual) key-rotation story.

Total to shippable: ~3–4 weeks — the bulk in the download/verify/extract pipeline and the catalog tooling,
not the dialog. New UI strings must go through `_()` into `wxn.pot` (the standing i18n gap).

## Pointers for whoever picks this up
- Menu/command: `src/command_ids.h` (Extensions range, next to `kCmdSettingOpenPluginsDir`), `src/menu_data_plugins.h`, `src/menu_labels_plugins.h`.
- Install-path ladder to reuse: `src/main.cpp` around `kCmdSettingOpenPluginsDir` (`<exe>/plugins` vs `userDataDir()/plugins` vs `<exe>/nib`).
- ABI gate: `include/nib/nib.h` (`NIB_ABI_VERSION`). Signing posture to mirror: `docs/SIGNING.md`.
- Tracking: `docs/MISSING_FUNCTIONALITY.md` (Plugins Admin row), user docs `site/docs/plugins.md` / `site/docs/menus.md`.
