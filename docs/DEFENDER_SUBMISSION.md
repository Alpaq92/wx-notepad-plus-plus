# Reporting a Defender false positive (maintainer runbook)

When Defender flags a wxNote installer, this is the procedure. It is a per-release chore: Microsoft's
determinations are scoped to **one file hash**, and the [software-developer
FAQ](https://learn.microsoft.com/en-us/defender-xdr/developer-faq) rules out product-level allow-listing
in as many words — *"Does Microsoft accept files for a known list or false-positive prevention program?
No. We don't accept these requests from software developers."* The only documented route to
product-level trust is a consistent code-signing certificate, which the project does not have.

So: every tagged release, every affected architecture, a fresh submission.

## Before submitting: get the facts from the affected machine

Do not submit from memory. Two commands produce everything the form asks for.

The detection record, including the exact security-intelligence version that produced the verdict:

```powershell
Get-MpThreatDetection | Sort-Object InitialDetectionTime -Descending | Select-Object -First 1 -Property *
```

The detection *path chain*, which is the single most informative artifact and is what turns a dispute
from assertion into evidence — Event 1116 is the detection, 1117 the action taken:

```powershell
Get-WinEvent -FilterHashtable @{ LogName='Microsoft-Windows-Windows Defender/Operational'; Id=1116 } -MaxEvents 5 | Format-List TimeCreated, Message
```

Read the `Path:` field carefully. `file:` / `webfile:` prefixes with **no `containerfile:` segment**
mean the detection is recorded against the outer installer PE, with no inner file named or implicated. A
`containerfile:` chain naming an inner member means the opposite, and the submission should say so.

## The submission

Portal: <https://www.microsoft.com/en-us/wdsi/filesubmission> · history: <https://www.microsoft.com/en-us/wdsi/submissionhistory>

- Sign in. The portal offers a **Skip** button, but without an account you cannot track status or
  receive the **developer contact form** that comes with the results — and that form is the escalation
  lever if the determination comes back wrong. A personal Microsoft account is enough.
- Submission type: **Software developer**. (Not "Home customer" — that route has no dispute path.)
- Classification: **Incorrectly detected as malware/malicious**.
- Attach the installer itself. The limit is 50 MB; wxNote's is well under it.
- Fill in the detection name and the security-intelligence version from the commands above.
- Paste the block below into **Additional information**. That field is capped at **1,900 characters**,
  so it is written to fit — check the count if you edit it.

Submit **every installer that has a detection record**, each one separately — they are different
hashes, and a determination on one does nothing for another. For v0.14.1 that means **both** the x64
and the ARM64 setup. Do not submit a build that was never flagged: the form requires a detection name
and the security-intelligence version that produced it, and there is no truthful way to fill those in
for a file Defender never objected to.

Check each one before submitting, using the download path rather than an on-demand scan — see the
measurement trap in [ANTIVIRUS.md](ANTIVIRUS.md). An on-demand scan reports what the local engine
already knows, and a cloud verdict may not have reached it yet, so a clean result there can be stale.

If you do scan by hand, pass `-DisableRemediation`. Without it Defender acts on what it finds and
deletes the installer — the one you need to attach to the submission.

The strongest argument available is the **`.zip` comparison**: the archive contains byte-for-byte the
same files the installer lays down, and it passes the same download-path check that the installer
fails. That isolates the verdict to the self-extracting stub rather than to anything wxNote ships, and
it is checkable by the analyst from the same release page.

### Additional-information block (fill the bracketed values per release)

```text
wxNote is an open-source (Apache-2.0) cross-platform text editor. This detection is a false positive.

File:      wxNote-[VERSION]-Setup.exe
SHA-256:   [HASH]
Detection: Trojan:Win32/Wacatac.B!ml (ThreatID 2147735505)
Security intelligence: [SI VERSION]   Engine: [ENGINE VERSION]

Source:  https://github.com/Alpaq92/wx-notepad-plus-plus
Tag:     v[VERSION]   Commit: [COMMIT SHA]
Built in public CI with published logs: [ACTIONS RUN URL]
The release publishes a SHA256SUMS file covering every asset.

Installer: NSIS 3.12, RequestExecutionLevel user, per-user install to
%LOCALAPPDATA%\Programs\wxNote. No elevation or self-elevation path, no service, no
scheduled task, no autostart, no HKLM writes, no shell extension or file-association
registration, no bundled third-party software, no telemetry, and no background network
activity. Nothing downloaded by the application is ever executed.

The payload is not detected -- only the installer stub is. wxnote.exe, nib\npp_bridge.dll
and the wxNote-[VERSION]-windows.zip carrying the identical files all pass on the same
machine, under the same definitions, checked through the same download path.
Defender Event 1116 records the detection against the outer PE with no containerfile:
chain, so no inner member is implicated.

Full behavioural disclosure: docs/ANTIVIRUS.md in the repository above.
```

## After a determination

Clients cache dynamic signatures, so a machine that already quarantined the file keeps blocking it
until they are refreshed. Microsoft's standard instruction is below — note that `MpCmdRun.exe` is not
on `PATH`, and both subcommands need an **elevated** prompt:

```powershell
& "$env:ProgramFiles\Windows Defender\MpCmdRun.exe" -removedefinitions -dynamicsignatures
& "$env:ProgramFiles\Windows Defender\MpCmdRun.exe" -SignatureUpdate
```

## Escalation, if the determination is wrong or never arrives

1. **The developer contact form** included with the submission results. Requires having signed in.
2. **winget.** Submitting to [microsoft/winget-pkgs](https://github.com/microsoft/winget-pkgs) runs
   the installer through a Microsoft-operated validation pipeline that includes a Defender scan. If it
   fires, the PR is labelled `Validation-Defender-Error`, and the documented remedy is to submit to the
   Defender team *and comment on the PR asking the Windows Package Manager team to investigate*. That
   is a Microsoft-internal escalation route otherwise unavailable to an unsigned project. See
   [PACKAGING_WINGET.md](PACKAGING_WINGET.md).
3. **VirusTotal.** Publish a scan link for each release. It creates a citable public record and is
   what other projects have used to establish false-positive status.

## A methodology warning worth more than any single remedy

Defender verdicts are per-hash, **sticky** (the cloud "blocks the file in all future encounters" once
it decides), and demonstrably **time-varying** with security-intelligence updates. Byte-identical
builds days apart have been reported getting opposite verdicts.

Therefore: *"we changed X and the next release was clean"* is not evidence that X worked. When testing
any candidate change, **re-download the previously-flagged asset at the same moment, from the same
machine and browser**.

The control is **asymmetric**, and the intuitive reading of it is the wrong way round:

- **New release clean, old one still flagged** — this is what the *null hypothesis already predicts*.
  The old verdict is bound to the old hash and stays; the new build is a new hash that starts unscored.
  You would see exactly this whether the change helped or did nothing at all. **It is not evidence.**
- **Old one now clean too** — Microsoft moved on their side, and any credit taken for the change is
  false. This is the only outcome the control can actually establish.

So the control cannot confirm a fix. It can only catch you crediting yourself for someone else's. That
is worth doing anyway, because the alternative is a project that accumulates folklore about which
packaging tweak "worked", each entry a coin flip remembered as a cause.

Note also that local scanning **cannot** validate a fix. Block-at-first-sight only consults the cloud
for executables carrying an Internet-zone mark, and a locally-built or locally-modified file is a
different, unknown hash. A one-byte-perturbed copy of a quarantined installer scans perfectly clean.
