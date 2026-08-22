# Signing & release integrity

wxNote's releases carry two kinds of trust signal: **checksums** (always) and **code
signatures** (when the maintainer has configured the certificates). This document is the
authoritative record of what is signed, what secrets drive it, and how a user verifies a
download.

The whole pipeline lives in `.github/workflows/release.yml` (checksums, GPG, Windows) and
`.github/workflows/build.yml` (macOS). Every signing step is **gated on a repository secret** and
no-ops with a `::notice::` when that secret is absent — so a fork or an unconfigured repo builds
and releases exactly as before, just with a `SHA256SUMS` added. Nothing here blocks a build.

## What ships today

| Artifact | Always | With the maintainer's secrets |
|---|---|---|
| `SHA256SUMS` (covers every asset) | ✅ published | — |
| `SHA256SUMS.asc` (detached GPG signature) | — | ✅ when the GPG key secret is set |
| Windows `*.exe` installers | unsigned | ✅ Authenticode-signed + timestamped |
| macOS `*.dmg` | unsigned | ✅ codesigned (Developer ID) + notarized + stapled |
| Linux `.deb`/`.rpm`/`.AppImage`/`.flatpak` | covered by `SHA256SUMS` (+ `.asc`) | — |

**Status.** `SHA256SUMS` is live now. The **GPG** path is complete and only needs its secret —
it is the one turnkey item on this page. The **Windows** step is wired but reads a `.pfx`, which
**no CA issues anymore** (see below): for a newly purchased certificate it is a dead end, and a
working route means workflow changes, not just a secret. The **macOS** path is scaffolded but has
**not yet been exercised against a real Developer ID certificate** — expect to refine the keychain/notarization steps the first time
it runs with real credentials.

## Verifying a download (end users)

Checksums (always available):

```sh
# from the folder holding the downloaded files + SHA256SUMS
sha256sum -c SHA256SUMS 2>/dev/null | grep -E 'wxNote|wxnote'   # each line should say: OK
```

GPG signature (when `SHA256SUMS.asc` is present):

```sh
# wxnote-signing-key.asc is attached to the same release as SHA256SUMS/SHA256SUMS.asc
gpg --import wxnote-signing-key.asc
gpg --verify SHA256SUMS.asc SHA256SUMS
```

(Importing a key that ships next to the thing it signs proves only that one signed the other. It is
worth exactly as much as your trust that the release page itself is genuine — which is why the
fingerprint is also printed in the release workflow's log, where it can be compared across releases.)

On **Windows**, the installer is per-user (`RequestExecutionLevel user`), so there is no UAC
elevation prompt at all — the dialog signing actually affects is **SmartScreen's** "Windows
protected your PC" gate on downloaded files, which names the publisher once the signature (and its
reputation) exists. On **macOS**, a notarized `.dmg` opens without the right-click → Open dance.

## Configuring signing (maintainer)

Add these under **Settings → Secrets and variables → Actions**. The GPG and Windows groups are
independent; add only the ones you have, and a missing group just skips. The five `APPLE_*` secrets
are **not** independent of each other: set all five or none. The cert pair alone signs but never
notarizes (Gatekeeper still blocks the result); the notarytool trio alone submits an *unsigned*
`.dmg`, which Apple rejects — and the release job fails with it.

### GPG signature of `SHA256SUMS` — free

Generate a key (no certificate authority, no cost), then export it:

```sh
gpg --quick-generate-key "wxNote Releases <you@example.com>" ed25519 sign 2y
gpg --armor --export-secret-keys <KEY_ID>          # -> paste into GPG_PRIVATE_KEY
gpg --armor --export <KEY_ID> > wxnote-public.asc  # attach to a release / publish for users
```

| Secret | Value |
|---|---|
| `GPG_PRIVATE_KEY` | ASCII-armored **private** key block |
| `GPG_PASSPHRASE`  | the key's passphrase (omit if the key has none) |

### Windows Authenticode — needs a certificate, and the `.pfx` route no longer exists

> **The `WINDOWS_PFX_BASE64` recipe below cannot be followed with a newly issued certificate.**
> Since the CA/Browser Forum baseline change of **June 2023**, publicly-trusted code-signing private
> keys must be generated on and never leave FIPS 140-2 Level 2+ hardware (an HSM, a USB token, or a
> cloud signing service). No CA will issue the exportable `.pfx` this workflow was written around.
> The step still works if you already hold a pre-2023 exportable certificate; for anything new, use
> one of the routes below instead. This section documented an impossible task for some time — that is
> why every release to date has shipped unsigned.

**Routes that work today**, cheapest first:

| Route | Cost | Notes |
|---|---|---|
| [SignPath Foundation](https://signpath.org/) | free for OSS | Built for exactly this case: public repo, OSI licence, builds in public CI. wxNote (Apache-2.0, public, GitHub Actions) fits the criteria. Signing happens on their infrastructure, so no key ever reaches the runner. |
| [Azure Trusted Signing](https://learn.microsoft.com/azure/trusted-signing/) | ~$10/month | Microsoft-operated PKI, short-lived certs. Good SmartScreen standing in practice, but Microsoft does **not** document an instant-reputation guarantee — do not buy on that promise. Its tooling signs on **Windows runners only** (the current step runs on Linux). Verify individual-developer eligibility first; it has been gated on years of verifiable identity history. |
| Certum Open Source | ~€100 / 2 years | Hardware card posted to you; signing is done locally, not in CI. |
| SSL.com eSigner / DigiCert KeyLocker | commercial | Cloud HSM; `osslsigncode` can drive these via `-pkcs11engine`/`-pkcs11module` from the existing Linux publish job — but that is a real workflow edit (a PKCS#11 module install plus new secrets replacing the `-pkcs12` invocation), not a flag change. |

Whichever route: the certificate subject must be a **real legal or individual identity**. "wxNote
Project" is not an entity that can hold an OV certificate, so whatever name goes on the certificate
should also replace it everywhere the publisher is named: `CompanyName` in `resources/app.rc.in`,
both `CompanyName` and the Add/Remove-Programs `Publisher` in `installer/windows/wxnote.nsi`, and
the `Publisher` fields in `installer/winget/*.yaml`.

**Sign inside-out.** The current step signs `dist/*.exe` — i.e. only the outer installer, after
`makensis` has already packed an unsigned `wxnote.exe` and `nib/npp_bridge.dll` inside it. Defender
scans the extracted payload at install time and on every launch, so a signed wrapper around unsigned
contents accrues no reputation for the installed application. Sign the payload on the Windows build
leg *before* packaging, then the installer.

The legacy `.pfx` secrets, for a pre-2023 exportable certificate only:

| Secret | Value |
|---|---|
| `WINDOWS_PFX_BASE64`   | base64 of the `.pfx` (`base64 -w0 wxnote.pfx`) |
| `WINDOWS_PFX_PASSWORD` | the `.pfx` password |

Signing runs from the Linux publish job via `osslsigncode` (no Windows runner needed) and timestamps
against DigiCert's TSA, so signatures stay valid after the certificate expires.

### macOS notarization — needs an Apple Developer membership

Requires the Apple Developer Program ($99/yr) and a **Developer ID Application** certificate. Export
it (with its private key) as a `.p12` and base64-encode it; create an app-specific password for
`notarytool` at appleid.apple.com.

| Secret | Value |
|---|---|
| `APPLE_CERT_BASE64`   | base64 of the Developer ID Application `.p12` |
| `APPLE_CERT_PASSWORD` | the `.p12` password |
| `APPLE_ID`            | the Apple ID email |
| `APPLE_TEAM_ID`       | the 10-char Team ID |
| `APPLE_APP_PASSWORD`  | an app-specific password for notarization |

The app is codesigned (deep, hardened runtime, secure timestamp) inside `installer/macos/build-dmg.sh`
before it is packed into the `.dmg`; the workflow then notarizes and staples the image.

## Why this is gated, not mandatory

Certificates cost money and identity setup a young, non-commercial project hasn't stood up yet (see
[`GOALS.md`](GOALS.md) → *Signing*). Rather than block releases on that, the pipeline ships integrity
today (checksums) and lights up each signature automatically the moment its secret exists — no code
change required.
