# Antivirus false positives, and what wxNote actually does

Windows Defender flagged the 0.14.0 installer as `Trojan:Win32/Wacatac.B!ml`. It is a false positive.
This document exists so that claim is checkable rather than asserted — it lists every privileged or
network-touching thing the program does, and the negative list of things it does not.

## What is actually flagged

Only the **installer stubs** — never the program. This was measured through the same code path a
browser uses when it saves a download (`IAttachmentExecute::Save`, which is what applies the
Mark-of-the-Web and triggers the download scan):

| Artifact | Verdict |
|---|---|
| `wxNote-<version>-Setup.exe` (x64) | **blocked** |
| `wxNote-<version>-arm64-Setup.exe` | **blocked** |
| `wxNote-<version>-windows.zip` | clean |
| `wxNote-<version>-arm64-windows.zip` | clean |
| `wxnote.exe` — the actual 12 MB program | **clean** |
| `nib/npp_bridge.dll` | **clean** |

Defender's own detection record (Event ID 1116) names the outer `.exe` with **no `containerfile:`
segment**, which is how Defender reports a detection found *inside* an archive. No inner file is
named, so nothing the installer carries is implicated: the verdict is recorded against the installer
wrapper as a single object, not against any code wxNote ships. The same files, shipped in a `.zip` instead of behind a self-extracting stub, are clean.

> **A measurement trap, recorded because we fell into it.** These verdicts are decided in Microsoft's
> cloud on the *download* path, and only then propagate into the local engine as a cached signature.
> Before that propagation an on-demand scan can call a file clean that a download would block: the
> ARM64 installer scanned clean at 08:55 and was blocked through the download path at 09:12. Re-tested
> back to back afterwards, both paths agreed — so the disagreement was a verdict that had not arrived
> yet, not two engines with different opinions. A clean on-demand result can simply be stale.
>
> Tools that fetch without setting Mark-of-the-Web — `curl`, `Invoke-WebRequest`, `gh release download`
> — never exercise the download path at all, which is why every row above was measured through
> `IAttachmentExecute::Save`, the call a browser makes when it saves a file. And if you scan by hand,
> pass `-DisableRemediation`:
>
> ```powershell
> & "$env:ProgramFiles\Windows Defender\MpCmdRun.exe" -Scan -ScanType 3 -File <path> -DisableRemediation
> ```
>
> Without that flag Defender acts on what it finds, and deletes the exact file you were trying to
> examine — including the one you need to attach to a false-positive report.

## Why it happens

The `!ml` suffix means the verdict came from a **machine-learning classifier**, not a signature match
against known malware. `Wacatac.B!ml` is the generic bucket for "shaped like a dropper", and it is the
single most commonly reported false positive for unsigned installers — an NSIS stub is a small program
whose job is to unpack a large compressed blob to disk and run it, which is also a fair description of
a real dropper.

Two properties drive it, and neither is about wxNote's behaviour:

1. **The installer is unsigned.** Authenticode signing is wired into the release pipeline but gated on
   a certificate the project does not have yet — see [SIGNING.md](SIGNING.md), which also explains why
   the previously-documented `.pfx` route stopped being possible in 2023. Without a signature there is
   no publisher identity for a classifier to weigh against any suspicious-looking feature.
2. **Every release is a brand-new file nobody has downloaded.** [Block at first
   sight](https://learn.microsoft.com/en-us/defender-endpoint/configure-block-at-first-sight-microsoft-defender-antivirus)
   sends internet-downloaded executables to Microsoft's cloud for a verdict. A binary first seen minutes
   ago, from a publisher with no identity, starts from zero.

The verdict is then **cached against that one file hash**, and Microsoft's documentation says the cloud
"blocks the file in all future encounters" once it decides.

That produces a pattern worth being precise about, because it is the whole reason this is hard to fix:

- **Every installer we have actually published is blocked** — both architectures, both releases. This
  is not an unlucky build.
- **Every hash the cloud has never seen comes back clean** — a local rebuild, the same installer with
  one byte changed, the CI payload repacked locally. All clean, because "unknown" is allowed, not
  because anything about them is better.

A build that has not been published yet is an **unscored hash**, so a clean result on it is
uninformative: it says the cloud has no opinion, not that the cloud would approve. That is the trap
waiting for anyone testing a candidate packaging fix — the encouraging local result arrives before the
verdict that matters, and looks exactly like success.

It is why this page recommends no packaging change: with the evidence available we cannot distinguish a
genuine improvement from a hash nobody has scored yet.

## What to do about it

- **Use the `.zip` instead.** Every Windows release ships a plain archive containing exactly the files
  the installer would lay down. Neither archive has ever had a detection record, and both pass the same
  download-path check the installers fail — and unlike a self-extracting stub, you can look inside
  before running anything. Extract it anywhere and run `wxnote.exe`. It is not "portable" in the strict
  sense — settings still go to the registry and your user-data directory — and you get no Start Menu
  entry, but the program is identical.
- **Verify the download** rather than trusting it. Every release ships `SHA256SUMS`; compare it:
  ```powershell
  Get-FileHash .\wxNote-<version>-windows.zip -Algorithm SHA256
  ```
  When `SHA256SUMS.asc` and `wxnote-signing-key.asc` are present you can check the GPG signature too
  (recipe in [SIGNING.md](SIGNING.md)).
- **Check the provenance.** Every artifact is built in public by GitHub Actions from a tagged commit.
  The workflow run, its logs and the exact commit are all linked from the release page.
- **Report it to Microsoft.** The [Security Intelligence submission
  portal](https://www.microsoft.com/en-us/wdsi/filesubmission) is the intended channel for an `!ml`
  verdict; submit as a software developer, and say it was incorrectly detected. Anyone affected can do
  this, not just us — independent reports carry weight. Microsoft publishes no turnaround target, and
  prioritises prevalent files and enterprise customers, so this can take days or considerably longer.
  Maintainers: the full procedure is in [DEFENDER_SUBMISSION.md](DEFENDER_SUBMISSION.md).

If a copy was already quarantined, Defender caches its verdict locally; refreshing the dynamic
signatures (`MpCmdRun.exe -removedefinitions -dynamicsignatures` then `-SignatureUpdate`) is Microsoft's
documented way to pick up a corrected determination.

We will not tell you to add an antivirus exclusion. That trains a habit worth more to an attacker
than to us.

## Everything privileged or networked that wxNote does

| What | When | Detail |
|---|---|---|
| **Asks Windows to save a protected file** | You save to a location your account cannot write | The bytes are staged in a temp file and the **shell** is asked to move them via COM `IFileOperation`. Windows shows its own shielded consent dialog naming the destination. wxNote itself never elevates and has no elevation switch. |
| **Loads plugin DLLs** | Startup | Scans `<install dir>/nib/*.dll`, requires the exported `nib_plugin_main` symbol and a matching ABI major version, unloads on mismatch. Only that one directory. |
| **Runs a shell in the integrated terminal** | You open the terminal panel | `CreateProcessW` with a pseudoconsole (ConPTY), running your configured shell, in a visible window. |
| **Runs a build/run command** | You press F5 and confirm | Runs the command *you* typed. |
| **Downloads dictionaries** | You click Download in the spell-check settings | HTTPS to `raw.githubusercontent.com`, fetching the `wooorm/dictionaries` word lists. Data only — nothing downloaded is ever executed. |
| **Relaunches itself** | You change UI language or theme mode | wxWidgets cannot re-theme a live window, so the app restarts to apply it. |
| **Opens files/URLs in another app** | You use "Open in browser", "Open containing folder" | `ShellExecuteW` with your default handler. |
| **Writes crash-recovery snapshots** | Every 30 s, while a file has unsaved edits | To your user-data directory, written to a temp file and renamed into place. |
| **Talks to an already-running copy** | You open a file while wxNote is running | Local IPC (`wxNote-IPC`) hands the filename to the existing window instead of starting a second one. Off unless you enable instance reuse. |

## What wxNote does not do

- No autostart, scheduled task, service, or any other persistence mechanism
- No writes outside your user profile (the installer is per-user; it never touches `HKLM`)
- No code downloaded and executed — the only download is dictionary word lists, on request
- No telemetry, analytics, crash reporting, or any background network activity
- No process injection, no hooking of other processes, no driver, no kernel component
- No file-association or shell-extension registration
- No bundled third-party software, toolbars, or offers

Every claim above is checkable against the source: this is an Apache-2.0 project and the build is
reproducible from the tagged commit.
