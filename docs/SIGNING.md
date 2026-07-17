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

**Status.** `SHA256SUMS` is live now. The GPG and Windows Authenticode paths are complete and
only need their secrets. The **macOS** path is scaffolded but has **not yet been exercised against
a real Developer ID certificate** — expect to refine the keychain/notarization steps the first time
it runs with real credentials.

## Verifying a download (end users)

Checksums (always available):

```sh
# from the folder holding the downloaded files + SHA256SUMS
sha256sum -c SHA256SUMS 2>/dev/null | grep -E 'wxNote|wxnote'   # each line should say: OK
```

GPG signature (when `SHA256SUMS.asc` is present):

```sh
gpg --recv-keys <KEY_ID>            # the project's public key (published on the Releases page)
gpg --verify SHA256SUMS.asc SHA256SUMS
```

On **Windows**, a signed installer shows a real publisher in the UAC prompt instead of "Unknown
publisher". On **macOS**, a notarized `.dmg` opens without the right-click → Open dance.

## Configuring signing (maintainer)

Add these under **Settings → Secrets and variables → Actions**. Each group is independent; add only
the ones you have. Missing groups just skip.

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

### Windows Authenticode — needs a paid certificate

Requires an OV/EV code-signing certificate (~$100–400/yr from a CA). Export it as a password-protected
`.pfx`/`.p12`, then base64-encode it:

```sh
base64 -w0 wxnote.pfx    # -> paste into WINDOWS_PFX_BASE64
```

| Secret | Value |
|---|---|
| `WINDOWS_PFX_BASE64`   | base64 of the `.pfx` |
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
