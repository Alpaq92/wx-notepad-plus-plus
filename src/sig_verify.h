// SPDX-License-Identifier: Apache-2.0
#pragma once
// =====================================================================
// sig_verify.h - dependency-light minisign signature verification over the vendored Monocypher
// (third_party/monocypher). Pure <cstdint>/<string> plus Monocypher - NO wx, NO OS crypto - so it is
// one code path on every platform and links into a headless test with nothing else.
//
// WHAT IT VERIFIES: minisign (jedisct1) detached signatures - the format the plugin catalog and
// release artifacts are signed with. Two flavours exist in the wild:
//
//   * "ED" (modern, the CLI's default): plain Ed25519 over the BLAKE2b-512 digest of the file.
//     CRITICAL: this is NOT RFC 8032 Ed25519ph. minisign hashes with BLAKE2b and signs the digest as
//     an ordinary message, so crypto_ed25519_check(sig, pk, digest, 64) is the correct call, and
//     crypto_ed25519_ph_check (SHA-512 based, with the "SigEd25519 no Ed25519 collisions" dom2
//     prefix) would reject every valid minisign signature ever made.
//   * "Ed" (legacy): plain Ed25519 over the raw file bytes.
//
// A .minisig document also carries a trusted comment and a "global" signature over
// (signature_bytes || trusted_comment_text). When present it MUST verify too - otherwise an attacker
// could splice the trusted comment (typically a filename + timestamp) from one validly-signed file
// onto another. A malformed global section is a parse FAILURE, never a silent downgrade to "no
// global signature".
//
// WHY HAND-ROLLED BASE64: base64 is a frozen, trivial codec (RFC 4648) whose strict decoder fits in
// ~30 exhaustively-testable lines; pulling in a library for it would add third-party provenance and
// buy nothing. Same doctrine as src/hash_algos.h.
//
// CONSTANT TIME: not required for verification - every input is public (file bytes, signatures,
// public keys, key ids). The only secret in the whole scheme is the maintainer's signing key, which
// never enters this process. We still never branch on anything secret, simply because nothing secret
// exists here to branch on.
// =====================================================================
#include <cstdint>
#include <cstring>
#include <string>
#include <array>   // only for the zero-length kTrustedKeys placeholder (empty raw arrays are ill-formed)

#include "monocypher.h"
#include "monocypher-ed25519.h"

namespace wxnsig {

struct PubKey
{
    uint8_t id[8];     // little-endian random key id, a public routing hint - not a fingerprint
    uint8_t key[32];   // Ed25519 public key
};

struct Signature
{
    uint8_t     id[8];
    uint8_t     sig[64];
    bool        prehashed = false;    // true = "ED" (Ed25519 over BLAKE2b-512 digest), false = legacy "Ed"
    std::string trustedComment;       // text after "trusted comment: ", exactly the bytes that were signed
    uint8_t     globalSig[64];        // Ed25519 over (sig || trustedComment)
    bool        hasGlobal = false;
};

namespace detail {

// --- strict base64 (RFC 4648), hand-rolled per the doctrine above ------------------------------

inline int b64Val(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

// Strict decode: length a multiple of 4, '=' only as the final one or two characters, and any byte
// outside the alphabet - including whitespace embedded mid-body - is a hard reject. A trailing
// "\r\n"/"\n" on the line is tolerated (lines arrive with their newline); nothing else is.
inline bool b64Decode(const std::string& in, std::string& out)
{
    size_t n = in.size();
    while (n > 0 && (in[n - 1] == '\r' || in[n - 1] == '\n')) --n;
    if (n == 0 || n % 4 != 0) return false;

    size_t pad = 0;
    if (in[n - 1] == '=') { ++pad; if (in[n - 2] == '=') ++pad; }

    out.clear();
    out.reserve(n / 4 * 3);
    for (size_t i = 0; i < n; i += 4) {
        const int chars = (i + 4 == n) ? 4 - static_cast<int>(pad) : 4;   // non-'=' chars in this group
        uint32_t acc = 0;
        for (int j = 0; j < 4; ++j) {
            const char c = in[i + j];
            if (j >= chars) {                       // padding position: must literally be '='
                if (c != '=') return false;
                acc <<= 6;
                continue;
            }
            const int v = b64Val(c);
            if (v < 0) return false;                // rejects '=' mid-body and all whitespace too
            acc = (acc << 6) | static_cast<uint32_t>(v);
        }
        out.push_back(static_cast<char>((acc >> 16) & 0xff));
        if (chars > 2) out.push_back(static_cast<char>((acc >> 8) & 0xff));
        if (chars > 3) out.push_back(static_cast<char>(acc & 0xff));
    }
    return true;
}

// --- tiny line reader: accepts LF and CRLF alike ------------------------------------------------

inline bool nextLine(const std::string& text, size_t& pos, std::string& line)
{
    if (pos >= text.size()) return false;
    const size_t nl = text.find('\n', pos);
    if (nl == std::string::npos) { line.assign(text, pos, text.size() - pos); pos = text.size(); }
    else                         { line.assign(text, pos, nl - pos);          pos = nl + 1; }
    if (!line.empty() && line.back() == '\r') line.pop_back();
    return true;
}

inline bool hasPrefix(const std::string& s, const char* p)
{
    const size_t n = std::strlen(p);
    return s.size() >= n && std::memcmp(s.data(), p, n) == 0;
}

} // namespace detail

// Parse the base64 payload line of a minisign .pub file (line 2 of the file - the caller strips the
// "untrusted comment:" line). Payload is 2-byte algo "Ed" + 8-byte key id + 32-byte public key.
inline bool parsePubKeyB64(const std::string& b64Line, PubKey& out)
{
    std::string raw;
    if (!detail::b64Decode(b64Line, raw)) return false;
    if (raw.size() != 2 + 8 + 32)         return false;
    if (raw[0] != 'E' || raw[1] != 'd')   return false;   // public keys are always algo "Ed"
    std::memcpy(out.id,  raw.data() + 2,  8);
    std::memcpy(out.key, raw.data() + 10, 32);
    return true;
}

// Parse a whole .minisig document (LF or CRLF):
//   line 1: "untrusted comment: ..."   - never signed, never trusted, but the prefix is required
//   line 2: base64(algo + key id + signature); algo "ED" = prehashed (BLAKE2b-512), "Ed" = legacy
//   line 3: "trusted comment: <text>"
//   line 4: base64(global signature over signature_bytes || <text>)
// Lines 3+4 are accepted as an all-or-nothing pair: absent entirely -> hasGlobal=false; present but
// malformed -> parse failure (see the downgrade note in the header comment).
inline bool parseMinisigDoc(const std::string& fileText, Signature& out)
{
    size_t pos = 0;
    std::string line, raw;

    if (!detail::nextLine(fileText, pos, line))            return false;
    if (!detail::hasPrefix(line, "untrusted comment:"))    return false;

    if (!detail::nextLine(fileText, pos, line))            return false;
    if (!detail::b64Decode(line, raw))                     return false;
    if (raw.size() != 2 + 8 + 64)                          return false;
    if      (raw[0] == 'E' && raw[1] == 'D') out.prehashed = true;    // modern: sign BLAKE2b-512(file)
    else if (raw[0] == 'E' && raw[1] == 'd') out.prehashed = false;   // legacy: sign the raw bytes
    else                                                   return false;
    std::memcpy(out.id,  raw.data() + 2,  8);
    std::memcpy(out.sig, raw.data() + 10, 64);
    out.trustedComment.clear();
    out.hasGlobal = false;

    if (!detail::nextLine(fileText, pos, line)) return true;              // no global section at all
    if (line.empty()) return !detail::nextLine(fileText, pos, line);      // lone trailing blank line

    if (!detail::hasPrefix(line, "trusted comment: "))     return false;
    out.trustedComment = line.substr(std::strlen("trusted comment: "));

    if (!detail::nextLine(fileText, pos, line))            return false;
    if (!detail::b64Decode(line, raw))                     return false;
    if (raw.size() != 64)                                  return false;
    std::memcpy(out.globalSig, raw.data(), 64);
    out.hasGlobal = true;
    return true;
}

inline bool keyIdMatches(const Signature& s, const PubKey& k)
{
    // Key ids are public routing hints (they say WHICH key to try, nothing more), so plain memcmp
    // is fine - there is no secret for its timing to leak.
    return std::memcmp(s.id, k.id, 8) == 0;
}

// True only if EVERY applicable check passes: key id match, the file signature, and - when the
// document carries one - the global signature over (sig || trustedComment).
inline bool verifyDetached(const uint8_t* data, size_t len, const Signature& s, const PubKey& k)
{
    if (!keyIdMatches(s, k)) return false;

    if (s.prehashed) {
        // minisign "ED": plain Ed25519 over the BLAKE2b-512 digest. Deliberately NOT
        // crypto_ed25519_ph_check - that is RFC 8032 Ed25519ph (SHA-512 + dom2 prefix), a different
        // scheme that would fail on every real minisign signature. See the header comment.
        uint8_t digest[64];
        crypto_blake2b(digest, 64, data, len);
        if (crypto_ed25519_check(s.sig, k.key, digest, 64) != 0) return false;
    } else {
        if (crypto_ed25519_check(s.sig, k.key, data, len) != 0) return false;
    }

    if (s.hasGlobal) {
        std::string msg(reinterpret_cast<const char*>(s.sig), 64);
        msg += s.trustedComment;
        if (crypto_ed25519_check(s.globalSig, k.key,
                                 reinterpret_cast<const uint8_t*>(msg.data()), msg.size()) != 0)
            return false;
    }
    return true;
}

// Compiled-in trust anchors. EMPTY on purpose: the real release key is generated by the maintainer
// with the stock minisign CLI and dropped in here when the plugin catalog goes live. Tests do not
// use this table at all - they pass their own PubKey values as parameters.
inline constexpr std::array<PubKey, 0> kTrustedKeys{};

} // namespace wxnsig
