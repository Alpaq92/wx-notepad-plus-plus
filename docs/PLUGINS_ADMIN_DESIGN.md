# Plugins Admin — design + build status

**Status: foundations BUILT, user-facing store still deferred.** Phases 0 and A have shipped (see
*Build status* below); everything from the browse dialog onward is still deliberately unbuilt, for the
original reason: an in-app plugin browser only pays off once a third-party plugin *ecosystem* exists,
and wxNote does not have one yet. A plugin store serves nobody until there are plugins to browse. The
foundations shipped anyway because each earns its keep independently — Phase 0 fixed a real bug (users
of *installed* builds could not add a plugin at all), and Phase A is pure library code with tests and no
user-visible surface.

## Build status

| Phase | State | What landed |
| --- | --- | --- |
| **0 — loader groundwork** | **DONE** (PR #110) | `loadNibPlugins` scans `<exe>/nib` then `<userDataDir>/nib` (bundled wins on name collision); `importPluginFiles()` targets the user dir and *reports* a failed copy; macOS import accepts `.so` (what CMake MODULE actually emits) as well as `.dylib`; **Open Plugins Folder** opens the writable dir; the loader also refuses a plugin whose declared Nib ABI *minor* exceeds the host's. |
| **A — catalog core** | **DONE** (PR #111) | `third_party/monocypher` (4.0.3, CC0 arm) + `src/sig_verify.h` (minisign verify) + `src/plugin_catalog.h` (parse + adversarial validation) + `src/json_value.h` (the JSON reader lifted out of `keymap_store.h`) + `tests/plugin_catalog_test.cpp` (83 checks, `pure` ctest, runs on all 8 CI legs). |
| **B — browse dialog** | not built | Gated on D10 below, and carries the i18n cost noted under *Phased plan*. |
| **C — install / uninstall** | not built | Needs Phase E's signing tooling to exist first (see the sequencing note). |
| **D — update detection** | not built | |
| **E — catalog repo + signing tooling** | not built | Needs the maintainer's real minisign key and decision D1. |

`installed.json` (named in the original Phase 0 scope) is **not** written yet — nothing installs
programmatically, so there is nothing to record. It belongs with Phase C.

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
  Nib dir (`userPluginDir()`, added in Phase 0), `npp-bridge` → whatever `pluginsRootW()` resolves to in
  `packages/npp-bridge/npp_bridge.cpp` (`<exe>\plugins` on Windows, `<user-data>/plugins` elsewhere) — and
  lets the UI grey out N++ plugins off Windows / when the GPL bridge isn't installed. **Note the conflict:**
  on Windows that bridge root is inside the read-only install dir, so `npp-bridge` entries are not
  installable there without either elevation or a bridge change — see decision D4.
- Each entry carries human-facing metadata (display name, description, author, homepage, **license badge**),
  a stable reverse-DNS `id`, `version`, `min/max-host-abi` (gated on `NIB_ABI_VERSION`), and an
  `install` block: `folder-name`, `binary`, `package` URL (a GitHub release asset), `sha256`, `size`.
  As built, **`min-host-abi` is required**: `abiSatisfied()` gates on major equality, so a defaulted `0`
  could never match a real host and an entry omitting the field would be silently uninstallable
  everywhere. The host-side gate was major-only until Phase 0 added the minor-version check.
- Binaries live as each plugin's own **GitHub release assets**; the catalog stores only URLs + hashes.

### Client (in-app)
- HTTP fetch for `index.json` → the matching per-arch list → on install, the `package` ZIP. The in-tree
  helpers are **synchronous** (`httpDownloadFile` / `httpGetText` in `src/main.cpp` use `wxWebRequestSync`,
  the same path the Hunspell dictionary download uses with a `wxBusyCursor`) — not async, as an earlier
  draft of this document claimed. Two caveats before leaning on them: they store to **memory**, which is
  fine for a ~1 MB dictionary and wrong for a plugin ZIP (use file storage with a staging path under
  `userDataDir()`), and `wxUSE_WEBREQUEST` can be **0** on Linux builds whose wx had no libcurl — so the
  dialog needs a documented degraded mode (Installed tab works, Available/Updates disabled), not just a
  disabled button.
- Pipeline: download → **check size** → **verify SHA-256** → verify signature (below) → extract →
  per-user plugins dir → mark restart-to-apply. Order matters: each check is strictly more expensive than
  the last, and nothing is extracted before the signature verifies. Extraction needs `wxZipInputStream`
  (`wxUSE_ZIPSTREAM`, part of wxBase and on by default) — **not** currently used anywhere in the tree, so
  it is a new dependency on that flag, not something already in use.
- A `wxDialog` with Available/Updates/Installed tabs.
- **Prerequisite (Phase 0, DONE):** the loader scans a per-user plugins root under `userDataDir()`,
  because installed builds cannot write under Program Files / `/opt` / the `.app` bundle. That rule is
  stated in the `userDataDir()` header comment (`src/main.cpp`, search `wxString userDataDir()`) — there
  is no separate named "gotcha" document, contrary to an earlier draft of this file.

### Trust — the one upgrade worth taking from the whole survey
Releases aren't code-signed yet, so a hash inside an unauthenticated JSON is anti-corruption, not
anti-tamper. Add a **detached Ed25519 signature sidecar** (`.minisig` / `.asc`) beside each artifact and the
catalog, verified against a **public key compiled into wxNote**. This upgrades trust from "trust the host"
to "trust the key."

**As built (Phase A):** the primitive is **Monocypher 4.0.3** (CC0 arm of its dual license), vendored as
four C99 files — chosen over libsodium (a full build system, not a vendorable file set) and over TweetNaCl
(unmaintained). Signature format is **minisign**, so the maintainer signs with the stock CLI and there is
no bespoke signing tool to write or keep secret-safe; `src/sig_verify.h` parses and verifies it in ~220
lines. Two implementation facts worth not rediscovering:
- minisign's `ED` mode is **plain Ed25519 over a BLAKE2b-512 digest** — *not* RFC 8032 Ed25519ph. Using
  `crypto_ed25519_ph_check` would reject every real signature.
- The **trusted comment and its global signature are mandatory**. While they were optional, anyone holding
  a validly signed `.minisig` could delete the last two lines and the truncated document still verified,
  un-binding the very comment the format exists to bind.

This is one place the repo's usual self-author doctrine (`src/hash_algos.h`) deliberately does **not**
apply: it rests on "exhaustively pinnable to published vectors", which is true of a frozen digest and
false of signature *verification*, where cofactor handling, small-order points and non-canonical encodings
all let a vector-passing implementation still accept forgeries.

Anti-rollback was pulled forward too: `index.json` carries a monotonic `serial`, and a lower one is
refused (`serialAcceptable`). A signed *stale* catalog is still a valid signature, so this is the one TUF
property worth cherry-picking after dropping TUF. (TUF via AWS `tough` solves key rotation and rollback,
but there is no C/C++ TUF client — reimplementing it is disproportionate for a solo-curated catalog.)
`kTrustedKeys` ships **empty**: the real release key is generated by the maintainer with the stock
minisign CLI when the catalog goes live.

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

## Phased plan
- **Phase 0 — loader groundwork — DONE.** Per-user plugins root scan + import/open-folder retargeting +
  the ABI minor gate. SHA-256 needed no vendoring: `src/hash_algos.h` already ships one, pinned to the
  NIST/RFC vectors by `tests/hash_test.cpp`.
- **Phase A — catalog core — DONE.** Fetch-independent: parse, validate, verify. No UI, no network, no
  new UI strings, which is exactly why it shipped before the ecosystem question is settled.
- **Phase B — browse dialog** (~450 LOC + wiring): Available/Updates/Installed tabs, license badges,
  greyed rows for wrong kind/arch/ABI, install button disabled. Menu id in the Extensions range next to
  `kCmdSettingOpenPluginsDir`.
- **Phase C — install / uninstall / restart** (~400 LOC): the download→verify→extract pipeline, scheduled
  uninstall (a mapped `.dll`/`.so` cannot be deleted in place), `installed.json`, restart-to-apply via
  `restartWithTheme()`.
- **Phase D — update detection** (~200 LOC): `installed.json` vs catalog version, Updates badge.
- **Phase E — catalog repo + signing tooling**: the `v1/index.json` + `v1/pl.*.json` tree, JSON-schema and
  hash-reachability CI, a `minisign`-driven signing step, Pages publish.

**Sequencing warning:** Phase E is last in *build* order but partly first in *operational* order — its key
generation and signed `index.json` must exist before Phase C can ship anything, and ideally before Phase B
so the dialog can be developed against a real signed fixture.

**The i18n cost is real and was previously understated.** An earlier draft called it "the standing i18n
gap"; it is now an **enforced build gate**. `tests/catalog_selftest.cpp` fails the build unless every
`_()` literal is a msgid in `wxn.pot` *and* present with a **non-empty** msgstr in all eight `.po` *and*
every committed `.mo` is the current compile of its `.po`, with the region-qualified twins byte-identical.
A dialog of Phase B's size is ~30 strings — roughly 240 translations, plus the recompile. Budget it per
UI phase; "leave it blank for now" is not an option the gate permits.

## Pointers for whoever picks this up
- Menu/command: `src/command_ids.h` (Extensions range, next to `kCmdSettingOpenPluginsDir`), `src/menu_data_plugins.h`, `src/menu_labels_plugins.h`.
- Install path: use `userPluginDir()` (Phase 0). Do **not** reuse the `kCmdSettingOpenPluginsDir` ladder as
  an install resolver — it is a best-effort *folder to open*, and its lower rungs deliberately fall back to
  directories that may not be writable.
- ABI gate: `include/nib/nib.h` (`NIB_ABI_VERSION`) and the loader's check in `loadNibPlugins`.
- Signing: **`docs/SIGNING.md` is a different mechanism** — a GPG signature over `SHA256SUMS`, gated on
  repo secrets, and not yet operational. It shares no machinery with the plugin catalog's compiled-in
  Ed25519/minisign key, which has its own lifecycle. Don't expect to reuse it.
- Crypto + catalog: `src/sig_verify.h`, `src/plugin_catalog.h`, `src/json_value.h`,
  `third_party/monocypher/`, `tests/plugin_catalog_test.cpp`.
- Tracking: `docs/MISSING_FUNCTIONALITY.md` (Plugins Admin row), user docs `site/docs/plugins.md` / `site/docs/menus.md`.

## Open decisions (for the maintainer)

Recorded so they are not re-litigated from scratch. Recommendations are the planning pass's, not commitments.

| # | Decision | Recommendation |
| --- | --- | --- |
| **D1** | Catalog repo: separate or in-tree? | **Separate** `wxnote-plugins` + GitHub Pages. "The PR is the trust event" only works if a catalog PR cannot touch `src/`; cadence differs from the app's. |
| **D2** | Ed25519 implementation | **Settled: Monocypher 4.0.3**, vendored (Phase A). |
| **D3** | Signature format | **Settled: minisign**, prehashed `ED` mode — stock CLI as the signer, free key-id for rotation. |
| **D4** | Windows `npp-bridge` install target is not user-writable | Grey out N++-kind entries for MVP; the real fix is teaching the bridge to prefer `<user-data>/plugins` on Windows too, which is a bridge change. |
| **D5** | Rollback/freeze protection | **Settled: shipped** — monotonic `serial` in the signed index. |
| **D6** | Key rotation | **Settled: shipped** — `kTrustedKeys` is an array from day one, so rotation is an app update, not a redesign. |
| **D7** | Loader enforcement of `min-host-abi` | **Settled: shipped** — the loader now refuses a newer declared minor (Phase 0). |
| **D8** | Where the i18n cost lands | Budget ~240 translations per UI phase; consider a `tools/` helper that seeds new msgids into all eight `.po`. |
| **D9** | Sync or async download | Sync + `wxBusyCursor` for MVP (matches the dictionary download), but **file** storage, not memory. |
| **D10** | **Is there anything to browse yet?** | **This gates Phases B–E.** A catalog with zero installable third-party entries is not shippable; seeding it with the already-bundled plugins exercises the pipeline but gives users nothing to install. |
