# Publishing wxNote to winget

Manifests live in [`installer/winget/`](../installer/winget/). They are kept in this repo as the
source of truth; publishing means copying them into a fork of
[microsoft/winget-pkgs](https://github.com/microsoft/winget-pkgs) and opening a PR.

## Why bother

Two reasons, neither of which is "it gets us past the antivirus detection":

1. **Discoverability and a one-command install/upgrade**, on a channel a lot of people already use,
   with `winget upgrade` handling later versions.
2. **A Microsoft-internal escalation route** — but not from the first submission, so do not plan
   around it yet. Submissions run through a Microsoft-operated pipeline that statically scans the
   installer and then installs it in a sandbox under Defender; if Defender fires, the PR is labelled
   `Validation-Defender-Error` and the documented remedy is to submit to the Defender team **and**
   comment on the PR asking the Windows Package Manager team to investigate. That is a channel a lone
   WDSI submission has no equivalent to. Note this cuts both ways: it is only reachable by submitting
   an asset that fires, which is also the asset that fails validation.

The original first reason given here — "a distribution path that is not the flagged asset, because
`winget install` fetches the `.zip`, which has never carried a detection" — was wrong on its stated
grounds and has been removed. The zip *has* since carried a detection (0.14.2 x64), so the premise is
simply false. Read on for what is actually true, which is more limited and partly unverified.

### Where the Defender gate does and does not apply

This decides which of the two things below you are reasoning about, and they are not the same:

**Getting listed — the gate applies, and this is what constrains the manifest.** The winget-pkgs
pipeline installs from the manifest as a local manifest, with no trusted source behind it. In that
path winget-cli calls `IAttachmentExecute::Save()` on the downloaded asset — the same call a browser
makes when saving a download, and the same oracle used to measure our artifacts — and for
`InstallerTypeEnum::Zip` exactly as for `nullsoft`:

```
DownloadInstaller (Zip -> DownloadInstallerFile)
  -> VerifyInstallerHash        [sets InstallerHashMatched]
  -> RenameDownloadedInstaller
  -> UpdateInstallerFileMotwIfApplicable
       -> ApplyMotwUsingIAttachmentExecuteIfApplicable(path, InstallerUrl, URLZONE_INTERNET)
```

On `E_FAIL` (`0x80004005` — the exact code a flagged artifact returns) it prints
`InstallerFailedVirusScan` and terminates with `APPINSTALLER_CLI_ERROR_INSTALLER_SECURITY_CHECK_FAILED`.
Sources: `src/AppInstallerCLICore/Workflows/DownloadFlow.cpp`, `src/AppInstallerCommonCore/Downloader.cpp`.
So **a flagged asset of either format fails validation**, and the same happens in the
`winget install --manifest` step of the checklist below. There is no format that is exempt.

**End users installing from the merged listing — the gate is skipped, but do not build on it.**
`VerifyInstallerHash` additionally sets `ContextFlag::InstallerTrusted` when the package's source has
`SourceTrustLevel::Trusted`, and `UpdateInstallerFileMotwIfApplicable` checks that flag *first*: it
then calls `ApplyMotwIfApplicable(path, URLZONE_TRUSTED)` and never reaches the `IAttachmentExecute`
branch. The default community source qualifies — measured with `winget source list --name winget`:
`Trust Level: Trusted|StoreOrigin`.

Be precise about what that does and does not mean:

- It is a statement about Microsoft's own client trusting its own signed source index. It is **not**
  evidence that the artifact is clean, and it does not disable Defender: real-time protection still
  applies when files are written and executed. We have **not** measured `winget install` from a
  merged listing against a known-flagged asset, so treat "it would install anyway" as *unverified*.
- Do **not** advertise winget as the way past an antivirus block, in release notes or on the site.
  It is a package listing, not a remedy, and the honest user-facing answer remains the one the FAQ
  gives: try the other format, and verify against `SHA256SUMS`.

### What a listing cannot fix

- The gate runs against the pipeline's definitions **at submission time**. A verdict that appears
  later is not caught by anything.
- Merged manifests are effectively immutable per version. Both 0.14.0 and 0.14.1 were clean at
  publication and were flagged afterwards, so an asset listed today can be flagged tomorrow, and the
  listing for that version cannot be edited to repair it; only a new version helps.
- wxNote is unsigned, and code signing is out of scope by project decision — noted here only so
  nobody re-derives it as the answer to the above.

What it is **not**: a reputation mechanism. The SmartScreen signals Microsoft documents are properties
of the file, its URL, the app and the signing certificate — package managers are not among them, and
for an unsigned file Microsoft states plainly that reputation must rebuild from zero for every new
version. winget installs do write Mark-of-the-Web and do count toward that file's prevalence,
which is a genuine if modest gain, but unsigned reputation **resets to zero every release** and cannot
carry forward. Do not claim in release notes that being on winget makes SmartScreen stand down.

## Which assets to list: decide per release, by measuring

The manifest in this repo is currently zip-only and pinned to **0.14.1**. Read that as "these are the
two Windows assets that measured clean", not as a format policy. The earlier rationale here — that the
NSIS stub is what gets flagged and the zip is inherently safe — is **disproven** and has been removed.

In v0.14.2, all four Windows assets from one CI run, measured within three minutes at the same
definitions:

| Asset | Verdict |
|---|---|
| `wxNote-0.14.2-Setup.exe` | clean |
| `wxNote-0.14.2-arm64-Setup.exe` | flagged (`Trojan:Win32/Wacatac.B!ml`) |
| `wxNote-0.14.2-windows.zip` | **flagged** (`Trojan:Script/Wacatac.B!ml`) |
| `wxNote-0.14.2-arm64-windows.zip` | clean |

Both formats land on both sides of the line inside a single release, and two archives whose contents
differ only by architecture get opposite verdicts. Nothing about the installer stub, self-extraction,
compression or NSIS explains that; every file added to the 0.14.2 zip is clean on its own, `wxnote.exe`
and `nib/npp_bridge.dll` have never been flagged, and the detection record names the archive as a
single object with no member inside it named. The verdict is **per-artifact and per-hash**, and is not
predictable from format or content.

So there is no format that can be picked in advance. Each release, measure, then list whichever
formats come back clean.

### How to measure

The oracle is the download path itself: `IAttachmentExecute::Save()`
(`CLSID 4125DD96-E03A-4103-8F70-E0597D803B9C`) — what a browser calls to save a file, what applies
Mark-of-the-Web, and, per the section above, what the validation pipeline and
`winget install --manifest` end up calling. `Save()` returning `0x80004005` with the file destroyed
means flagged; `0x00000000` means clean.

Three traps, each of which has produced a wrong answer here before:

- **`gh`, `Invoke-WebRequest` and `WebClient` do not set Mark-of-the-Web** (verified: no
  `Zone.Identifier` stream), so they never exercise this path and will happily fetch a flagged file.
- **`MpCmdRun -Scan` is not the oracle.** It reports only what the local engine already knows, which
  can be stale, and it *remediates* — it deletes the file — unless `-DisableRemediation` is passed.
- **"Unknown" is not "clean."** A hash nothing has scored yet reads clean; a one-byte-perturbed copy of
  a flagged installer measures clean, and locally rebuilt installers measure clean. This is also why a
  candidate fix cannot be validated before release: any new build has a new hash and will read clean
  regardless of whether it fixed anything.

And a clean result is not durable. Measure as close to submission as possible, and expect that a
listed version may be flagged later with nothing you can do about it (see above).

### Listing both formats, or a mixed set

Nothing requires the manifest to be single-format. `InstallerType`, `NestedInstallerType`,
`NestedInstallerFiles` and `ArchiveBinariesDependOnPath` can all be set per-installer, so an
architecture whose zip is flagged can be listed as `nullsoft` while the other stays `zip`. That shape
was checked with `winget validate` against schema 1.12.0 and passes:

```yaml
Installers:
- Architecture: x64
  InstallerType: nullsoft
  Scope: user
  InstallerUrl: https://github.com/Alpaq92/wx-notepad-plus-plus/releases/download/v<VER>/wxNote-<VER>-Setup.exe
  InstallerSha256: <UPPERCASE SHA256>
  InstallerSwitches:
    Silent: /S
    SilentWithProgress: /S
  AppsAndFeaturesEntries:
  - DisplayName: wxNote
    Publisher: wxNote Project
    ProductCode: wxNote
- Architecture: arm64
  InstallerType: zip
  NestedInstallerType: portable
  ArchiveBinariesDependOnPath: true
  InstallerUrl: https://github.com/Alpaq92/wx-notepad-plus-plus/releases/download/v<VER>/wxNote-<VER>-arm64-windows.zip
  InstallerSha256: <UPPERCASE SHA256>
  NestedInstallerFiles:
  - RelativeFilePath: wxNote-<VER>-arm64\wxnote.exe
    PortableCommandAlias: wxnote
```

Prefer the explicit per-installer form over inheriting from the manifest root whenever the entries are
not all the same type — the root-level form also validated locally, but the per-installer form leaves
nothing for the community repo's extra rules to disagree about.

`ProductCode: wxNote` matches the `HKCU\...\Uninstall\wxNote` key the NSIS script writes, which is how
winget correlates an existing install for upgrades.

When both types are listed and the user expresses no preference, winget applies a **built-in
installer-type precedence** (`ManifestComparator`, roughly MSStore → Msix → Msi → Wix → Burn →
Nullsoft → Inno → Exe → Portable) rather than manifest order, and a `zip`+`portable` entry ranks last.
So ordering inside `Installers:` is a readability choice, not a control — do not "fix" a wrong
selection by reordering. Users can override with `--installer-type` or
`installBehavior.preferences.installerTypes`.

That default is the one to want where it is available: the NSIS path creates Start Menu shortcuts and
registers wxNote's own Add/Remove Programs entry with its own uninstaller, which the portable path does
not.

## Submitting

The community repo requires the **multi-file** manifest form (singleton manifests are rejected), with
all three files named after the package identifier, under a path whose segments must match
`PackageIdentifier` and `PackageVersion` exactly:

```
manifests/w/wxNoteProject/wxNote/<version>/
  wxNoteProject.wxNote.yaml
  wxNoteProject.wxNote.installer.yaml
  wxNoteProject.wxNote.locale.en-US.yaml
```

One package version per pull request, and every file in the PR must live under `manifests/`.

Validate locally before opening the PR. Know what this does and does not cover: `winget validate`
never downloads anything, so it checks schema and manifest semantics **only**. It does *not* verify
`InstallerSha256` — a manifest with a deliberately zeroed hash still reports "Manifest validation
succeeded" (measured, client v1.29.280). Hash errors surface only in `winget install --manifest` or in
the pipeline.

```powershell
winget validate --manifest .\manifests\w\wxNoteProject\wxNote\<VER>\
```

Installing from the local manifest is what actually exercises the download, the hash check and the
Defender gate. It requires the local-manifest feature to be enabled first, in an **elevated** prompt,
or the command is refused:

```powershell
winget settings --enable LocalManifestFiles
winget install --manifest .\manifests\w\wxNoteProject\wxNote\<VER>\
```

[`wingetcreate`](https://github.com/microsoft/winget-create) automates the fork/branch/PR mechanics and
can update hashes and URLs for a new version. Pass whichever URLs the measurement step selected — it
has no idea any of that happened, and will cheerfully re-point the manifest at a flagged asset:

```powershell
wingetcreate update wxNoteProject.wxNote --version <VER> --urls <x64-url> <arm64-url> --submit
```

It is also the tool least likely to get `RelativeFilePath` right; re-read it afterwards.

## Per-release checklist

- [ ] **Measure the candidate Windows assets first, and let the result choose the formats.** Run all
      four (`Setup.exe`, `arm64-Setup.exe`, `windows.zip`, `arm64-windows.zip`) through the
      `IAttachmentExecute::Save()` oracle described above — not `gh`, not `MpCmdRun`. List, per
      architecture, a format that came back clean; use the mixed-format shape if the two architectures
      disagree. **Do not assume the zip.** In 0.14.2 the x64 zip was the flagged one. If *both*
      formats for an architecture are flagged, do not submit that architecture — a listing that
      terminates with `InstallerFailedVirusScan` on the user's machine is worse than no listing.
- [ ] Bump `PackageVersion` in **all three** files — they must agree with each other and with the path.
- [ ] Update both `InstallerUrl`s and both `InstallerSha256`s (uppercase; take them from the release's
      `SHA256SUMS`, which is published for every release — note it is **unsigned**: no release has
      ever shipped `SHA256SUMS.asc`, so it establishes integrity against GitHub, not authenticity).
- [ ] Update `RelativeFilePath` on any `zip` entry — **this is the easy one to miss.** The zip's root
      folder is versioned (`wxNote-<VER>/`, and `wxNote-<VER>-arm64/` for ARM; it comes from the
      `build/zipstage/wxNote-$ver$arch` staging path in `.github/workflows/build.yml`), so this string
      changes every single release, and it is the field automated update tooling is least reliable at
      rewriting. If the zip root is ever made unversioned, this stops being a trap.
- [ ] Update `ReleaseDate` (the release's `published_at`, UTC) and `ReleaseNotesUrl`.
- [ ] `winget validate` locally for schema, then `winget install --manifest` to actually exercise the
      hash and the Defender gate. Then open one PR for that version.

## If validation fails

| Label | Meaning | Action |
|---|---|---|
| `Validation-Defender-Error` | Defender fired on the downloaded asset. **This can be a `.zip` entry, not just the `.exe`** — the verdict is per-hash, not per-format | Report it as a false positive at <https://www.microsoft.com/en-us/wdsi/filesubmission> (submission type: software developer), then comment on the PR asking the Windows Package Manager team to investigate. Precedent exists for a re-run passing ~24 h later. Re-running without a change is otherwise pointless: the same hash gets the same verdict. If the other format for that architecture measures clean, switching the entry is the faster fix. |
| `Binary-Validation-Error` | A static scanner flagged the asset | Same route. |
| `Error-Hash-Mismatch` | `InstallerSha256` does not match the downloaded file | Re-take the hash from `SHA256SUMS`. |
| `Manifest-Path-Error` | Path segments disagree with the identifier/version | Fix the directory. |
| `Validation-Unattended-Failed` | Silent install did not complete non-interactively | For the NSIS entries, check `/S`. |
| `URL-Validation-Error` | A URL in the manifest failed reputation filtering | Usually transient; ask for a re-run. |
