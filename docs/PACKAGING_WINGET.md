# Publishing wxNote to winget

Manifests live in [`installer/winget/`](../installer/winget/). They are kept in this repo as the
source of truth; publishing means copying them into a fork of
[microsoft/winget-pkgs](https://github.com/microsoft/winget-pkgs) and opening a PR.

## Why bother

Two concrete reasons, one of which is not about convenience:

1. **A distribution path that is not the flagged asset.** `winget install wxNoteProject.wxNote`
   fetches the `.zip`, which has never carried a detection, and never touches the NSIS stub that does.
2. **Later, a Microsoft-internal escalation route** — but not from the first submission, so do not
   plan around it yet. Submissions run through a Microsoft-operated pipeline that statically scans the
   installer and then installs it in a sandbox under Defender; if Defender fires, the PR is labelled
   `Validation-Defender-Error` and the documented remedy is to submit to the Defender team **and**
   comment on the PR asking the Windows Package Manager team to investigate. That is a channel a lone
   WDSI submission has no equivalent to — but the pipeline only fetches what is in `InstallerUrl`, and
   the manifest below is zip-only. Until the `nullsoft` entries are added, the flagged installer is
   never downloaded, never scanned, and cannot produce that label. The first submission buys reason 1
   and nothing else.

What it is **not**: a reputation mechanism. The SmartScreen signals Microsoft documents are properties
of the file, its URL, the app and the signing certificate — package managers are not among them, and
for an unsigned file Microsoft states plainly that reputation must rebuild from zero for every new
version. winget installs do write Mark-of-the-Web and do count toward that file's prevalence,
which is a genuine if modest gain, but unsigned reputation **resets to zero every release** and cannot
carry forward. Do not claim in release notes that being on winget makes SmartScreen stand down.

## Why the manifest is zip-only right now

The installer manifest deliberately ships **only** the `zip` installers, not the NSIS `.exe`.

The validation pipeline downloads the exact `InstallerUrl` and executes it in a clean sandbox with
Defender live. `wxNote-<version>-Setup.exe` currently carries an active cloud verdict, so including it
would fail validation and block the listing outright — including the zip entries that would otherwise
have passed. Getting listed with a working install path beats being unlisted while arguing.

Once the false positive is cleared (see [DEFENDER_SUBMISSION.md](DEFENDER_SUBMISSION.md)), add the
NSIS entries in a follow-up PR. They will become the default automatically: when the user expresses no
preference, winget applies a **built-in installer-type precedence** (`ManifestComparator`, roughly
MSStore → Msix → Msi → Wix → Burn → Nullsoft → Inno → Exe → Portable) rather than manifest order, and a
`zip`+`portable` entry ranks last. So ordering inside `Installers:` is a readability choice, not a
control — do not "fix" a wrong selection by reordering. Users can override with `--installer-type` or
`installBehavior.preferences.installerTypes`.

That default is the one we want: the NSIS path creates Start Menu shortcuts and registers wxNote's own
Add/Remove Programs entry with its own uninstaller, which the portable path does not.

```yaml
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
  # ...same, with the -arm64 asset
```

`ProductCode: wxNote` matches the `HKCU\...\Uninstall\wxNote` key the NSIS script writes, which is how
winget correlates an existing install for upgrades.

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

Validate locally before opening the PR — this catches schema and hash errors without burning a
pipeline run:

```powershell
winget validate --manifest .\manifests\w\wxNoteProject\wxNote\0.14.1\
```

Installing from the local manifest additionally exercises the real install path. It requires the
local-manifest feature to be enabled first, in an **elevated** prompt, or the command is refused:

```powershell
winget settings --enable LocalManifestFiles
winget install --manifest .\manifests\w\wxNoteProject\wxNote\0.14.1\
```

[`wingetcreate`](https://github.com/microsoft/winget-create) automates the fork/branch/PR mechanics and
can update hashes and URLs for a new version:

```powershell
wingetcreate update wxNoteProject.wxNote --version <VER> --urls <zip-x64-url> <zip-arm64-url> --submit
```

## Per-release checklist

- [ ] Bump `PackageVersion` in **all three** files — they must agree with each other and with the path.
- [ ] Update both `InstallerUrl`s and both `InstallerSha256`s (uppercase; take them from the release's
      `SHA256SUMS`).
- [ ] Update `RelativeFilePath` — **this is the easy one to miss.** The zip's root folder is versioned
      (`wxNote-0.14.1/`, and `wxNote-0.14.1-arm64/` for ARM), so this string changes every single
      release, and it is the field automated update tooling is least reliable at rewriting. If the zip
      root is ever made unversioned, this stops being a trap.
- [ ] Update `ReleaseDate` and `ReleaseNotesUrl`.
- [ ] `winget validate` locally, then open one PR for that version.

## If validation fails

| Label | Meaning | Action |
|---|---|---|
| `Validation-Defender-Error` | Defender fired during the sandbox install | Submit to WDSI per [DEFENDER_SUBMISSION.md](DEFENDER_SUBMISSION.md), then comment on the PR asking the Windows Package Manager team to investigate. Precedent exists for a re-run passing ~24 h later. |
| `Binary-Validation-Error` | A static scanner flagged the installer | Same route. |
| `Error-Hash-Mismatch` | `InstallerSha256` does not match the downloaded file | Re-take the hash from `SHA256SUMS`. |
| `Manifest-Path-Error` | Path segments disagree with the identifier/version | Fix the directory. |
| `Validation-Unattended-Failed` | Silent install did not complete non-interactively | For the NSIS entries, check `/S`. |
| `URL-Validation-Error` | A URL in the manifest failed reputation filtering | Usually transient; ask for a re-run. |
