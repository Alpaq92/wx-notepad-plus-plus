# Antivirus false positives, and what wxNote actually does

Windows Defender flagged the 0.14.0 installer as `Trojan:Win32/Wacatac.B!ml`. It is a false positive.
This document exists so that claim is checkable rather than asserted — it lists every privileged or
network-touching thing the program does, and the negative list of things it does not.

## Why it happens

The `!ml` suffix means the verdict came from a **machine-learning heuristic**, not a signature match
against known malware. `Wacatac.B!ml` is the generic bucket Defender uses for "this looks like the
shape of a dropper" and is the single most commonly reported false positive for unsigned installers.

Two properties drive it, and neither is about wxNote's behaviour:

1. **The installer is unsigned.** Authenticode signing is wired into the release pipeline but gated
   on a certificate the project does not have yet — see [SIGNING.md](SIGNING.md), which also explains
   why the previously-documented `.pfx` route stopped being possible in 2023. Without a signature
   there is no publisher identity, so a classifier has nothing to weigh against any suspicious-looking
   feature.
2. **Every release is a brand-new file nobody has downloaded.** Reputation systems score prevalence.
   A binary first seen minutes ago, from a publisher with no identity, starts from zero.

Supporting evidence that this is publisher-level rather than code-level: the same detection ID hit an
unrelated project by the same author, built in a different language and packaged with a different
installer generator. The only thing the two share is being unsigned and new.

**Signing is the fix.** Everything else on this page is honesty, not remedy.

## What to do about it

- **Verify the download** rather than trusting it. Every release ships `SHA256SUMS`; compare it:
  ```powershell
  Get-FileHash .\wxNote-<version>-Setup.exe -Algorithm SHA256
  ```
  When `SHA256SUMS.asc` and `wxnote-signing-key.asc` are present you can check the GPG signature too
  (recipe in [SIGNING.md](SIGNING.md)).
- **Check the provenance.** Every artifact is built in public by GitHub Actions from a tagged commit.
  The workflow run, its logs and the exact commit are all linked from the release page.
- **Report it to Microsoft.** The [Security Intelligence submission
  portal](https://www.microsoft.com/en-us/wdsi/filesubmission) is the intended channel for an `!ml`
  verdict; submit as a software developer. This is usually resolved within days.
- **Use the `.zip` instead.** Windows releases also ship a plain archive with the same files the
  installer lays down. It is not "portable" — settings still go to the registry and your user-data
  directory — but it involves no installer stub, and you can look inside it before running anything.

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
