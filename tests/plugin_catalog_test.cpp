// SPDX-License-Identifier: Apache-2.0
//
// plugin_catalog_test - headless self-test for the Plugins-Admin catalog parser/validators
// (src/plugin_catalog.h) and the minisign verification path (src/sig_verify.h). Pure, no wx, no
// filesystem, no RNG, no clock: every signature fixture is created IN MEMORY at test start from
// fixed 32-byte seeds via the vendored Monocypher, so runs are bit-for-bit deterministic on all
// 8 CI targets. The one external anchor is the RFC 8032 TEST 1 known-answer vector, which pins
// the vendored Ed25519 to the standard rather than to itself.
//
//   cmake --build build --target plugin_catalog_test && build/bin/plugin_catalog_test
//
#include "plugin_catalog.h"
#include "sig_verify.h"

#include "monocypher-ed25519.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int g_fail = 0, g_pass = 0;
static void check(bool ok, const char* what) { std::printf(ok ? "  ok    %s\n" : "  FAIL  %s\n", what); if (ok) ++g_pass; else ++g_fail; }

// --- tiny local helpers (test-side only) ---------------------------------------------------------

// Minimal RFC 4648 base64 ENCODER - the test-side twin of sig_verify.h's strict decoder, used to
// hand-build .minisig documents from raw bytes.
static std::string b64(const uint8_t* p, size_t n)
{
    static const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (size_t i = 0; i < n; i += 3) {
        uint32_t acc = static_cast<uint32_t>(p[i]) << 16;
        if (i + 1 < n) acc |= static_cast<uint32_t>(p[i + 1]) << 8;
        if (i + 2 < n) acc |= p[i + 2];
        out += tbl[(acc >> 18) & 63];
        out += tbl[(acc >> 12) & 63];
        out += (i + 1 < n) ? tbl[(acc >> 6) & 63] : '=';
        out += (i + 2 < n) ? tbl[acc & 63] : '=';
    }
    return out;
}

static int hexVal(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}
static void hexToBytes(const char* hex, uint8_t* out, size_t n)
{
    for (size_t i = 0; i < n; ++i)
        out[i] = static_cast<uint8_t>((hexVal(hex[2 * i]) << 4) | hexVal(hex[2 * i + 1]));
}

// --- deterministic key fixtures ------------------------------------------------------------------

// FIXED seeds (exactly 32 bytes each; the static_asserts count the NUL). No RNG anywhere.
static const char kSeedTextA[] = "wxnote-catalog-test-key-A-000001";
static const char kSeedTextB[] = "wxnote-catalog-test-key-B-000002";
static_assert(sizeof kSeedTextA == 33, "seed A must be exactly 32 bytes");
static_assert(sizeof kSeedTextB == 33, "seed B must be exactly 32 bytes");

// Keys A and B deliberately share one key id: the "wrong key" tests below must prove the CRYPTO
// rejects a foreign signature, not merely the key-id routing hint.
static const uint8_t kKeyId[8] = { 0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6, 0x07, 0x18 };

struct KeyMat { uint8_t secret[64]; wxnsig::PubKey pub; };

static KeyMat makeKey(const char* seedText)
{
    KeyMat k;
    uint8_t seed[32];                       // Monocypher WIPES the seed buffer it is handed
    std::memcpy(seed, seedText, 32);
    crypto_ed25519_key_pair(k.secret, k.pub.key, seed);
    std::memcpy(k.pub.id, kKeyId, 8);
    return k;
}

// minisign "ED" flavour: plain Ed25519 over the BLAKE2b-512 digest of the payload (NOT Ed25519ph).
static wxnsig::Signature signPrehashed(const KeyMat& k, const std::string& payload)
{
    wxnsig::Signature s;
    std::memcpy(s.id, k.pub.id, 8);
    s.prehashed = true;
    uint8_t digest[64];
    crypto_blake2b(digest, 64, reinterpret_cast<const uint8_t*>(payload.data()), payload.size());
    crypto_ed25519_sign(s.sig, k.secret, digest, 64);
    return s;
}

// minisign legacy "Ed" flavour: plain Ed25519 over the raw payload bytes.
static wxnsig::Signature signLegacy(const KeyMat& k, const std::string& payload)
{
    wxnsig::Signature s;
    std::memcpy(s.id, k.pub.id, 8);
    s.prehashed = false;
    crypto_ed25519_sign(s.sig, k.secret,
                        reinterpret_cast<const uint8_t*>(payload.data()), payload.size());
    return s;
}

// Global signature over (signature_bytes || trusted_comment_text), exactly as minisign defines it.
static void addGlobal(wxnsig::Signature& s, const KeyMat& k, const std::string& comment)
{
    s.trustedComment = comment;
    std::string msg(reinterpret_cast<const char*>(s.sig), 64);
    msg += comment;
    crypto_ed25519_sign(s.globalSig, k.secret,
                        reinterpret_cast<const uint8_t*>(msg.data()), msg.size());
    s.hasGlobal = true;
}

static std::string buildMinisigDoc(const wxnsig::Signature& s, bool crlf, bool withGlobal)
{
    const char* nl = crlf ? "\r\n" : "\n";
    uint8_t line2[2 + 8 + 64];
    line2[0] = 'E';
    line2[1] = s.prehashed ? 'D' : 'd';
    std::memcpy(line2 + 2, s.id, 8);
    std::memcpy(line2 + 10, s.sig, 64);
    std::string doc = std::string("untrusted comment: signature from plugin_catalog_test") + nl
                    + b64(line2, sizeof line2) + nl;
    if (withGlobal) {
        doc += std::string("trusted comment: ") + s.trustedComment + nl;
        doc += b64(s.globalSig, 64) + nl;
    }
    return doc;
}

// --- catalog fixtures ----------------------------------------------------------------------------

static const char* kGoodSha = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";

// One-entry target list with every install field swappable - the adversarial harness. Values are
// spliced into JSON text verbatim, so callers pass PRE-ESCAPED fragments (e.g. C++ "a\\\\b" is the
// JSON text a\\b, i.e. the value a\b).
static wxnplug::ParseResult tryEntry(const std::string& folder, const std::string& binary,
                                     const std::string& url, const std::string& sha,
                                     const std::string& sizeLit)
{
    const std::string doc =
        std::string("{\"schema\":1,\"entries\":[{\"id\":\"com.example.bad\",\"version\":\"1.0\",")
        + "\"kind\":\"nib\",\"min-host-abi\":\"1.0\",\"install\":{\"folder-name\":\"" + folder + "\",\"binary\":\"" + binary
        + "\",\"package\":\"" + url + "\",\"sha256\":\"" + sha + "\",\"size\":" + sizeLit + "}}]}";
    std::vector<wxnplug::Entry> es;
    return wxnplug::parseTargetList(doc, es);
}

int main()
{
    std::printf("plugin_catalog_test\n");

    const KeyMat keyA = makeKey(kSeedTextA);
    const KeyMat keyB = makeKey(kSeedTextB);
    const std::string payload = "{\"schema\":1,\"entries\":[]}\n";   // stands in for catalog bytes

    std::printf("[minisig parsing: strict base64 + document structure]\n");
    {
        // .pub payload line: "Ed" + key id + public key
        uint8_t pubRaw[2 + 8 + 32];
        pubRaw[0] = 'E'; pubRaw[1] = 'd';
        std::memcpy(pubRaw + 2, kKeyId, 8);
        std::memcpy(pubRaw + 10, keyA.pub.key, 32);
        wxnsig::PubKey pk;
        check(wxnsig::parsePubKeyB64(b64(pubRaw, sizeof pubRaw), pk)
              && std::memcmp(pk.id, kKeyId, 8) == 0
              && std::memcmp(pk.key, keyA.pub.key, 32) == 0,
              "pub key base64 line round-trips id + key bytes");
        pubRaw[1] = 'D';   // "ED" is a signature algo, never a public key algo
        check(!wxnsig::parsePubKeyB64(b64(pubRaw, sizeof pubRaw), pk),
              "pub key line with algo \"ED\" rejected");

        wxnsig::Signature made = signPrehashed(keyA, payload);
        addGlobal(made, keyA, "timestamp:1755820800 file:pl.windows-x86_64.json");

        wxnsig::Signature got;
        check(wxnsig::parseMinisigDoc(buildMinisigDoc(made, false, true), got),
              "well-formed LF .minisig doc parses");
        check(got.prehashed
              && std::memcmp(got.id, made.id, 8) == 0
              && std::memcmp(got.sig, made.sig, 64) == 0,
              "algo/keyid/signature bytes round-trip");
        check(got.hasGlobal
              && got.trustedComment == made.trustedComment
              && std::memcmp(got.globalSig, made.globalSig, 64) == 0,
              "trusted comment + global signature round-trip");

        wxnsig::Signature crlfGot;
        check(wxnsig::parseMinisigDoc(buildMinisigDoc(made, true, true), crlfGot)
              && std::memcmp(crlfGot.sig, made.sig, 64) == 0 && crlfGot.hasGlobal,
              "CRLF .minisig doc accepted");

        wxnsig::Signature noGlobal;
        check(!wxnsig::parseMinisigDoc(buildMinisigDoc(made, false, false), noGlobal),
              "doc with the trusted-comment lines STRIPPED is rejected (comment-strip attack)");

        wxnsig::Signature junk;
        check(!wxnsig::parseMinisigDoc("untrusted comment: x\n!!not*base64!!\n", junk),
              "garbage base64 on line 2 rejected");

        uint8_t shortRaw[2 + 8 + 63];              // one byte short of a real signature payload
        shortRaw[0] = 'E'; shortRaw[1] = 'D';
        std::memcpy(shortRaw + 2, kKeyId, 8);
        std::memcpy(shortRaw + 10, made.sig, 63);
        check(!wxnsig::parseMinisigDoc("untrusted comment: x\n" + b64(shortRaw, sizeof shortRaw) + "\n", junk),
              "truncated line-2 payload rejected");
    }

    std::printf("[signature verification: prehashed, legacy, tamper, wrong key]\n");
    {
        const uint8_t* data = reinterpret_cast<const uint8_t*>(payload.data());
        const size_t   len  = payload.size();

        wxnsig::Signature bare = signPrehashed(keyA, payload);
        check(!wxnsig::verifyDetached(data, len, bare, keyA.pub),
              "a signature with no global section is refused outright");

        wxnsig::Signature s = signPrehashed(keyA, payload);
        addGlobal(s, keyA, "timestamp:1755820800 file:payload");
        check(wxnsig::verifyDetached(data, len, s, keyA.pub), "good prehashed (\"ED\") signature verifies");

        std::string tampered = payload;
        tampered[0] ^= 0x01;
        check(!wxnsig::verifyDetached(reinterpret_cast<const uint8_t*>(tampered.data()),
                                      tampered.size(), s, keyA.pub),
              "single flipped payload byte fails");

        wxnsig::Signature flipped = s;
        flipped.sig[10] ^= 0x01;
        check(!wxnsig::verifyDetached(data, len, flipped, keyA.pub), "single flipped signature byte fails");

        wxnsig::Signature foreign = signPrehashed(keyB, payload);   // same key id, different key
        addGlobal(foreign, keyB, "timestamp:1755820800 file:payload");   // self-consistent global, so
                                                                         // only the crypto can reject it
        check(!wxnsig::verifyDetached(data, len, foreign, keyA.pub),
              "signature under a different seed's key fails (crypto, not key-id routing)");

        wxnsig::Signature wrongId = s;
        wrongId.id[0] ^= 0xFF;
        check(!wxnsig::verifyDetached(data, len, wrongId, keyA.pub), "key id mismatch fails");

        wxnsig::Signature g = signPrehashed(keyA, payload);
        addGlobal(g, keyA, "timestamp:1755820800 file:index.json");
        check(wxnsig::verifyDetached(data, len, g, keyA.pub), "global signature present and valid verifies");

        wxnsig::Signature gTampered = g;                            // splice attack: edit the comment
        gTampered.trustedComment += " oops";
        check(!wxnsig::verifyDetached(data, len, gTampered, keyA.pub),
              "trusted comment edited after signing fails the global signature");

        wxnsig::Signature legacy = signLegacy(keyA, payload);
        addGlobal(legacy, keyA, "timestamp:1755820800 file:payload");
        check(wxnsig::verifyDetached(data, len, legacy, keyA.pub), "legacy \"Ed\" over raw bytes verifies");
        check(!wxnsig::verifyDetached(reinterpret_cast<const uint8_t*>(tampered.data()),
                                      tampered.size(), legacy, keyA.pub),
              "legacy flavour also fails on a flipped payload byte");

        // end-to-end: bytes -> .minisig doc -> parse -> verify
        wxnsig::Signature reparsed;
        check(wxnsig::parseMinisigDoc(buildMinisigDoc(g, false, true), reparsed)
              && wxnsig::verifyDetached(data, len, reparsed, keyA.pub),
              "doc built, parsed, and verified end-to-end");
    }

    std::printf("[RFC 8032 section 7.1 known-answer vectors]\n");
    {
        // TEST 1: empty message. NOTE the exact triple, checked against the RFC text: the empty-
        // message signature e5564300... belongs to secret seed 9d61b19d... / public key
        // d75a9801...; the seed 4ccd089b... / public key 3d4017c3... pair is TEST 2 (message 0x72)
        // and is exercised right below with ITS published signature. Mixing the TEST 2 key with
        // the TEST 1 signature is a known transcription trap - it can never verify.
        uint8_t seed[32], expectedPub[32], sig[64];
        hexToBytes("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60", seed, 32);
        hexToBytes("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", expectedPub, 32);
        hexToBytes("e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb88215"
                   "90a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b", sig, 64);

        uint8_t sec[64], pub[32];
        crypto_ed25519_key_pair(sec, pub, seed);                    // consumes (wipes) `seed`
        check(std::memcmp(pub, expectedPub, 32) == 0, "TEST 1: secret seed reproduces the published public key");
        check(crypto_ed25519_check(sig, pub, reinterpret_cast<const uint8_t*>(""), 0) == 0,
              "TEST 1: published signature verifies over the empty message");

        uint8_t badSig[64];
        std::memcpy(badSig, sig, 64);
        badSig[0] ^= 0x01;
        check(crypto_ed25519_check(badSig, pub, reinterpret_cast<const uint8_t*>(""), 0) != 0,
              "TEST 1: flipped bit in the known-answer signature fails");

        wxnsig::PubKey pk;
        std::memcpy(pk.id, kKeyId, 8);
        std::memcpy(pk.key, pub, 32);
        wxnsig::Signature s;
        std::memcpy(s.id, kKeyId, 8);
        std::memcpy(s.sig, sig, 64);
        s.prehashed = false;                                        // KAT is raw-bytes Ed25519
        // verifyDetached requires the (now-mandatory) global section; the RFC publishes none, but
        // it DOES publish the secret seed, so mint one with the vector's own key.
        s.trustedComment = "rfc8032 test 1";
        {
            std::string gm(reinterpret_cast<const char*>(s.sig), 64);
            gm += s.trustedComment;
            crypto_ed25519_sign(s.globalSig, sec,
                                reinterpret_cast<const uint8_t*>(gm.data()), gm.size());
            s.hasGlobal = true;
        }
        check(wxnsig::verifyDetached(reinterpret_cast<const uint8_t*>(""), 0, s, pk),
              "TEST 1: same vector passes through wxnsig::verifyDetached (legacy path)");

        // TEST 2: one-byte message 0x72, the 4ccd089b.../3d4017c3... keypair.
        uint8_t seed2[32], expectedPub2[32], sig2[64];
        hexToBytes("4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb", seed2, 32);
        hexToBytes("3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c", expectedPub2, 32);
        hexToBytes("92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da"
                   "085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00", sig2, 64);
        uint8_t sec2[64], pub2[32];
        crypto_ed25519_key_pair(sec2, pub2, seed2);
        const uint8_t msg2[1] = { 0x72 };
        check(std::memcmp(pub2, expectedPub2, 32) == 0, "TEST 2: secret seed reproduces the published public key");
        check(crypto_ed25519_check(sig2, pub2, msg2, 1) == 0,
              "TEST 2: published signature verifies over the 0x72 message");
    }

    std::printf("[parseIndex + serial anti-rollback]\n");
    {
        wxnplug::Index idx;
        wxnplug::ParseResult pr = wxnplug::parseIndex(
            "{ \"schema\": 1, \"serial\": 42, \"generated\": \"2026-08-22T12:00:00Z\","
            "  \"targets\": [\"windows-x86_64\", \"linux-riscv64\"], \"future-field\": [1, 2] }", idx);
        check(pr.ok, "good index parses (unknown top-level field ignored)");
        check(idx.schema == 1 && idx.serial == 42ull, "schema + serial land");
        check(idx.generated == "2026-08-22T12:00:00Z", "generated lands");
        check(idx.targets.size() == 2 && idx.targets[0] == "windows-x86_64"
              && idx.targets[1] == "linux-riscv64", "targets land in order");

        pr = wxnplug::parseIndex("{ \"schema\": 1, \"generated\": \"x\" }", idx);
        check(!pr.ok, "index without serial rejected");
        check(pr.error.find("serial") != std::string::npos, "error names the missing field");

        // Absurd magnitudes must fail the RANGE CHECK, never reach the integer casts (an
        // out-of-range double-to-int conversion is undefined behavior; 1e400 arrives as inf).
        pr = wxnplug::parseIndex("{ \"schema\": 1e400, \"serial\": 1 }", idx);
        check(!pr.ok, "schema 1e400 rejected before the cast");
        pr = wxnplug::parseIndex("{ \"schema\": 1, \"serial\": 1e30 }", idx);
        check(!pr.ok, "serial 1e30 rejected before the cast");

        check(!wxnplug::serialAcceptable(5, 4), "serial 5 -> 4 is a rollback: rejected");
        check(wxnplug::serialAcceptable(5, 5), "serial 5 -> 5 is a re-fetch of current: accepted");
        check(wxnplug::serialAcceptable(5, 6), "serial 5 -> 6 is an update: accepted");
    }

    std::printf("[parseTargetList: good document]\n");
    {
        const std::string doc = std::string(
            "{ \"schema\": 1, \"entries\": [\n"
            "  { \"id\": \"com.example.hexviewer\", \"version\": \"1.2.3\", \"kind\": \"nib\",\n"
            "    \"name\": \"Hex Viewer\", \"description\": \"View any file as hex\",\n"
            "    \"author\": \"Example Dev\", \"homepage\": \"https://example.com/hexviewer\",\n"
            "    \"license\": \"MIT\", \"min-host-abi\": \"1.6\", \"max-host-abi\": \"1.9\",\n"
            "    \"future-field\": { \"nested\": true },\n"
            "    \"install\": { \"folder-name\": \"hexviewer\", \"binary\": \"hexviewer.nib\",\n"
            "      \"package\": \"https://github.com/example/hexviewer/releases/download/v1.2.3/hexviewer-windows-x86_64.zip\",\n"
            "      \"sha256\": \"") + kGoodSha + "\", \"size\": 123456, \"future-install-field\": 7 } },\n"
            "  { \"id\": \"org.legacy.tool\", \"version\": \"0.9\", \"kind\": \"npp-bridge\",\n"
            "    \"min-host-abi\": \"1.0\",\n"
            "    \"install\": { \"folder-name\": \"legacytool\", \"binary\": \"legacytool.dll\",\n"
            "      \"package\": \"https://release-assets.githubusercontent.com/example/legacytool.zip\",\n"
            "      \"sha256\": \"" + kGoodSha + "\", \"size\": 1 } }\n"
            "] }";

        std::vector<wxnplug::Entry> es;
        wxnplug::ParseResult pr = wxnplug::parseTargetList(doc, es);
        check(pr.ok && es.size() == 2, "good two-entry list parses (unknown fields ignored)");
        if (es.size() == 2) {
            const wxnplug::Entry& e0 = es[0];
            check(e0.id == "com.example.hexviewer" && e0.version == "1.2.3"
                  && e0.kind == wxnplug::Kind::Nib, "entry 1: identity fields land");
            check(e0.name == "Hex Viewer" && e0.description == "View any file as hex"
                  && e0.author == "Example Dev" && e0.homepage == "https://example.com/hexviewer"
                  && e0.licenseSpdx == "MIT", "entry 1: display metadata lands");
            check(e0.minAbi == 0x10006u, "\"min-host-abi\": \"1.6\" packs to 0x10006");
            check(e0.maxAbi == 0x10009u, "\"max-host-abi\": \"1.9\" packs to 0x10009");
            check(e0.install.folderName == "hexviewer" && e0.install.binary == "hexviewer.nib"
                  && e0.install.sha256 == kGoodSha && e0.install.size == 123456ull
                  && e0.install.packageUrl.find("https://github.com/") == 0,
                  "entry 1: install block lands");
            const wxnplug::Entry& e1 = es[1];
            check(e1.kind == wxnplug::Kind::NppBridge && e1.install.binary == "legacytool.dll"
                  && e1.install.size == 1ull, "entry 2: npp-bridge kind + install land");
            check(e1.minAbi == 0x10000u && e1.maxAbi == 0xFFFFFFFFu,
                  "entry 2: required min lands; absent max stays an open upper bound");
        }

        const std::string noSha = std::string(
            "{ \"schema\": 1, \"entries\": [ { \"id\": \"com.example.nosha\", \"version\": \"1.0\","
            "  \"kind\": \"nib\", \"install\": { \"folder-name\": \"nosha\", \"binary\": \"nosha.nib\","
            "  \"package\": \"https://github.com/e/r/releases/download/v1/n.zip\", \"size\": 10 } } ] }");
        std::vector<wxnplug::Entry> es2;
        pr = wxnplug::parseTargetList(noSha, es2);
        check(!pr.ok, "entry without sha256 rejected");
        check(pr.error.find("com.example.nosha") != std::string::npos, "error names the offending entry id");

        const std::string noMin = std::string(
            "{ \"schema\": 1, \"entries\": [ { \"id\": \"com.example.nomin\", \"version\": \"1.0\","
            "  \"kind\": \"nib\", \"install\": { \"folder-name\": \"nomin\", \"binary\": \"nomin.nib\","
            "  \"package\": \"https://github.com/e/r/releases/download/v1/n.zip\","
            "  \"sha256\": \"") + kGoodSha + "\", \"size\": 10 } } ] }";
        std::vector<wxnplug::Entry> es3;
        pr = wxnplug::parseTargetList(noMin, es3);
        check(!pr.ok && pr.error.find("min-host-abi") != std::string::npos,
              "entry without min-host-abi rejected by name (a defaulted 0 could never install)");
    }

    std::printf("[adversarial install specs: traversal, hosts, hashes, sizes]\n");
    {
        const std::string url = "https://github.com/wxnote/demo/releases/download/v1.0/demo.zip";
        check(tryEntry("demo", "demo.nib", url, kGoodSha, "1024").ok,
              "baseline good entry accepted (control)");

        // Zip-slip / traversal names: these become paths under the per-user plugins root that the
        // installer creates and the UNinstaller recursively deletes - reject or lose a home dir.
        check(!tryEntry("..",       "demo.nib", url, kGoodSha, "1024").ok, "folder-name \"..\" rejected");
        check(!tryEntry("a/b",      "demo.nib", url, kGoodSha, "1024").ok, "folder-name with '/' rejected");
        check(!tryEntry("a\\\\b",   "demo.nib", url, kGoodSha, "1024").ok, "folder-name with '\\' rejected");
        check(!tryEntry("C:evil",   "demo.nib", url, kGoodSha, "1024").ok, "folder-name with drive ':' rejected");
        check(!tryEntry(".hidden",  "demo.nib", url, kGoodSha, "1024").ok, "folder-name starting with '.' rejected");
        check(!tryEntry(std::string(65, 'a'), "demo.nib", url, kGoodSha, "1024").ok,
              "65-byte folder-name rejected");

        check(!tryEntry("demo", "demo.nib", "http://github.com/x/y.zip",         kGoodSha, "1024").ok,
              "plain-http package URL rejected");
        check(!tryEntry("demo", "demo.nib", "https://evil.com/x.zip",            kGoodSha, "1024").ok,
              "non-allowlisted host rejected");
        check(!tryEntry("demo", "demo.nib", "https://github.com@evil.com/x.zip", kGoodSha, "1024").ok,
              "userinfo '@' authority trick rejected");
        check(!tryEntry("demo", "demo.nib", "https://github.com:8443/x.zip",     kGoodSha, "1024").ok,
              "explicit port rejected");
        check(!tryEntry("demo", "demo.nib", "https://github.com/a\r\nHost: evil", kGoodSha, "1024").ok,
              "CR/LF inside the package URL rejected (request-splitting shape)");

        // Windows-reality names: a trailing dot/space collapses ("plug " opens the same directory
        // as "plug"), device names are files in EVERY directory, wildcards are shell hazards.
        check(!tryEntry("plug ",   "demo.nib", url, kGoodSha, "1024").ok, "trailing space in folder-name rejected");
        check(!tryEntry("plug.",   "demo.nib", url, kGoodSha, "1024").ok, "trailing dot in folder-name rejected");
        check(!tryEntry("NUL",     "demo.nib", url, kGoodSha, "1024").ok, "reserved device name rejected");
        check(!tryEntry("demo",    "con.dll",  url, kGoodSha, "1024").ok, "reserved device stem in binary rejected");
        check(!tryEntry("a*b",     "demo.nib", url, kGoodSha, "1024").ok, "wildcard character rejected");

        check(!tryEntry("demo", "demo.nib", url,
                        "E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855", "1024").ok,
              "uppercase sha256 rejected (lowercase-only policy)");
        check(!tryEntry("demo", "demo.nib", url, std::string(kGoodSha).substr(0, 63), "1024").ok,
              "63-char sha256 rejected");
        check(!tryEntry("demo", "demo.nib", url, std::string(kGoodSha).substr(0, 63) + "g", "1024").ok,
              "non-hex char in sha256 rejected");

        check(!tryEntry("demo", "demo.nib", url, kGoodSha, "0").ok,         "size 0 rejected");
        check(!tryEntry("demo", "demo.nib", url, kGoodSha, "209715201").ok, "size above the 200 MiB cap rejected");
    }

    std::printf("[hostile JSON: 200-deep nesting fails cleanly]\n");
    {
        std::string deep(200, '[');
        deep.append(200, ']');
        wxnplug::Index idx;
        wxnplug::ParseResult pr = wxnplug::parseIndex(deep, idx);
        check(!pr.ok && !pr.error.empty(), "parseIndex: 200-deep array rejected (kMaxDepth), no crash");
        std::vector<wxnplug::Entry> es;
        pr = wxnplug::parseTargetList(deep, es);
        check(!pr.ok, "parseTargetList: 200-deep array rejected, no crash");

        // \u escape hardening in the shared reader: a surrogate PAIR combines into one real UTF-8
        // sequence (separate halves would be CESU-8, which makes wxString::FromUTF8 drop the whole
        // field downstream), and a lone half or U+0000 becomes U+FFFD instead.
        wxnplug::Index sIdx;
        check(wxnplug::parseIndex(
                  "{ \"schema\": 1, \"serial\": 1, \"generated\": \"\\uD83D\\uDE00\" }", sIdx).ok
              && sIdx.generated == "\xF0\x9F\x98\x80",
              "surrogate pair decodes to one 4-byte UTF-8 sequence");
        check(wxnplug::parseIndex(
                  "{ \"schema\": 1, \"serial\": 1, \"generated\": \"a\\uD83Db\" }", sIdx).ok
              && sIdx.generated == "a\xEF\xBF\xBD" "b",
              "lone high surrogate becomes U+FFFD");
        check(wxnplug::parseIndex(
                  "{ \"schema\": 1, \"serial\": 1, \"generated\": \"a\\u0000b\" }", sIdx).ok
              && sIdx.generated == "a\xEF\xBF\xBD" "b",
              "embedded \\u0000 becomes U+FFFD (it would truncate c_str consumers)");
    }

    std::printf("[targetSlug, abiSatisfied, installable]\n");
    {
        static const char* const kKnown[] = {
            "windows-x86_64", "windows-arm64", "windows-x86", "linux-x86_64",
            "linux-arm64", "linux-riscv64", "macos-arm64", "macos-x86_64",
        };
        const char* slug = wxnplug::targetSlug();
        bool found = false;
        for (const char* k : kKnown)
            if (std::strcmp(slug, k) == 0) found = true;
        char lbl[96];
        std::snprintf(lbl, sizeof lbl, "targetSlug() = \"%s\" is one of the 8 CI target names", slug);
        check(found, lbl);

        const uint32_t host = (1u << 16) | 6;    // a 1.6 host, i.e. NIB_ABI_VERSION packing
        wxnplug::Entry e;

        e.minAbi = 0x10000u; e.maxAbi = 0xFFFFFFFFu;
        bool wide = wxnplug::abiSatisfied(e, host);
        e.minAbi = 0x10006u; e.maxAbi = 0x10006u;
        check(wide && wxnplug::abiSatisfied(e, host), "host inside window accepted; both bounds inclusive");
        e.minAbi = 0x10007u; e.maxAbi = 0xFFFFFFFFu;
        check(!wxnplug::abiSatisfied(e, host), "entry needing a newer minor (1.7) rejected on a 1.6 host");
        e.minAbi = 0x10000u; e.maxAbi = 0x10005u;
        check(!wxnplug::abiSatisfied(e, host), "entry capped below the host (max 1.5) rejected");
        e.minAbi = 0x00006u; e.maxAbi = 0xFFFFFFFFu;
        check(!wxnplug::abiSatisfied(e, host), "major-0 entry rejected on a major-1 host despite minAbi <= host");
        e.minAbi = 0x20000u; e.maxAbi = 0x2FFFFu;
        check(!wxnplug::abiSatisfied(e, host), "major-2 entry rejected on a major-1 host");

        wxnplug::Entry nib;    nib.kind    = wxnplug::Kind::Nib;
        wxnplug::Entry bridge; bridge.kind = wxnplug::Kind::NppBridge;
        check(wxnplug::installable(nib, false) && wxnplug::installable(nib, true),
              "nib entries installable regardless of the bridge");
        check(!wxnplug::installable(bridge, false) && wxnplug::installable(bridge, true),
              "npp-bridge entries gated on the bridge being present");
    }

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
