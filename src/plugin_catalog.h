// SPDX-License-Identifier: Apache-2.0
#pragma once
// =====================================================================
// plugin_catalog.h - Plugins-Admin catalog parsing + validation (docs/PLUGINS_ADMIN_DESIGN.md,
// "Catalog"). Pure <cstdint>/<string>/<vector> + json_value.h - NO wx - so it is one code path on
// every platform and links into a headless test with nothing else.
//
// Two documents, both static JSON files behind the Ed25519 signed-catalog trust gate
// (src/sig_verify.h):
//   * index.json          - { "schema", "serial", "generated", "targets": ["windows-x86_64", ...] }
//   * pl.<os>-<arch>.json - { "schema", "entries": [ ... ] }, one per CI target
//
// PARSING POSTURE - adversarial and all-or-nothing. Signature verification runs BEFORE this parser,
// so any bytes that reach it are authenticated as the maintainer's. A malformed or policy-violating
// entry inside a *signed* catalog is therefore a publishing bug (or a compromised signing pipeline)
// - never something to best-effort half-load. One bad entry fails the whole parseTargetList, with
// an error naming the entry and field so the publishing bug is a one-line fix on the catalog side.
// Unknown EXTRA fields, by contrast, are ignored everywhere (forward compatibility: an old client
// must keep working when the catalog grows new metadata).
// =====================================================================
#include <cstdint>
#include <string>
#include <vector>

#include "json_value.h"

namespace wxnplug {

// Routes the install path: Nib -> the per-user Nib dir; NppBridge -> the plugins dir, loadable only
// through the GPL npp-bridge plugin.
enum class Kind { Nib, NppBridge };

struct InstallSpec
{
    std::string folderName;             // directory created under the per-user plugins root
    std::string binary;                 // the shared library inside that directory
    std::string packageUrl;             // immutable GitHub Release-asset URL (never a mutable tag)
    std::string sha256;                 // lowercase hex digest of the package
    unsigned long long size = 0;        // exact package size in bytes
};

struct Entry
{
    std::string id;                     // stable reverse-DNS id, [a-z0-9.-]
    std::string version;
    std::string name, description, author, homepage, licenseSpdx;   // display-only metadata
    Kind        kind   = Kind::Nib;
    uint32_t    minAbi = 0;             // NIB_ABI_VERSION packing: (major << 16) | minor
    uint32_t    maxAbi = 0xFFFFFFFFu;
    InstallSpec install;
};

struct Index
{
    int                      schema = 0;
    unsigned long long       serial = 0;    // monotonic anti-rollback counter
    std::string              generated;
    std::vector<std::string> targets;
};

struct ParseResult
{
    bool        ok = false;
    std::string error;                  // names the offending entry id/field on failure
};

// Hard cap on a plugin package. Anything larger in the catalog is a publishing mistake, and the cap
// bounds what a (hypothetically compromised) catalog can make every client download.
inline constexpr unsigned long long kMaxPackageSize = 200ull * 1024ull * 1024ull;

namespace detail {

// --- field validators, each a single tight policy -----------------------------------------------

inline bool validId(const std::string& s)
{
    if (s.empty() || s.size() > 128) return false;
    for (char c : s)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-'))
            return false;
    return true;
}

// folderName / binary are joined into filesystem paths under userDataDir()'s plugins root during
// install, and REMOVED from there on uninstall. That makes them the zip-slip / path-traversal
// surface of this whole feature: a value like "../../../home/user" would let a catalog entry write
// outside the plugins root - or worse, aim the uninstaller's recursive delete at the user's HOME
// DIRECTORY. No legitimate plugin name needs a separator, a drive colon, a leading dot, or "..",
// so all of them are hard rejects, not sanitized.
inline bool validPathComponent(const std::string& s)
{
    if (s.empty() || s.size() > 64)          return false;
    if (s[0] == '.')                         return false;  // blocks ".", "..", and hidden dotfiles
    if (s.find("..") != std::string::npos)   return false;  // anywhere, not just as a full component
    // Windows strips a trailing dot or space when opening a path, so "plug" and "plug " resolve to
    // the SAME directory while the catalog would treat them as distinct entries.
    if (s.back() == '.' || s.back() == ' ')  return false;
    // Explicit ALLOWLIST, not a blocklist: [A-Za-z0-9._ -] and nothing else. This is what rejects
    // separators, drive colons, NULs, control bytes and the Windows wildcard/pipe set without
    // having to enumerate them - enumerating rejects is how one gets forgotten.
    for (char c : s)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '.' || c == '_' || c == '-' || c == ' '))
            return false;
    // Windows reserved device names are files in EVERY directory, and the reservation is on the
    // STEM: "con.dll" is still CON. Case-insensitive by definition.
    std::string stem = s.substr(0, s.find('.'));
    for (char& c : stem) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 32);
    static const char* const kReserved[] = { "CON", "PRN", "AUX", "NUL",
        "COM1","COM2","COM3","COM4","COM5","COM6","COM7","COM8","COM9",
        "LPT1","LPT2","LPT3","LPT4","LPT5","LPT6","LPT7","LPT8","LPT9" };
    for (const char* r : kReserved) if (stem == r) return false;
    return true;
}

// Package URLs must be https on an exact-match allowlisted host. The authority is everything
// between "https://" and the next '/': any userinfo '@' (https://github.com@evil.com/ really
// connects to evil.com) or ':' port (sidesteps the intent of pinning the well-known hosts) is a
// hard reject rather than something to parse around.
inline bool validPackageUrl(const std::string& url)
{
    static const char* const kAllowedHosts[] = {
        "github.com",                              // release-asset redirect origin
        "objects.githubusercontent.com",           // where the redirect lands
        "release-assets.githubusercontent.com",    // newer asset CDN host
    };

    if (url.size() > 2048) return false;                     // sanity bound
    // Printable ASCII only, over the WHOLE url - not just the authority. An embedded CR/LF
    // ("https://github.com/a\r\nHost: evil") passes a scheme+authority check because the authority
    // stops at the first '/', and if any later layer ever builds a request line or header from
    // this string, that is request splitting. Nothing on the allowlisted hosts needs non-ASCII.
    for (unsigned char c : url)
        if (c <= 0x20 || c >= 0x7F) return false;

    const std::string scheme = "https://";
    if (url.size() <= scheme.size() || url.compare(0, scheme.size(), scheme) != 0) return false;

    const size_t slash = url.find('/', scheme.size());
    if (slash == std::string::npos) return false;            // no path = no artifact to point at

    const std::string authority = url.substr(scheme.size(), slash - scheme.size());
    if (authority.empty())                              return false;
    if (authority.find('@') != std::string::npos)       return false;  // userinfo trick
    if (authority.find(':') != std::string::npos)       return false;  // explicit port

    for (const char* host : kAllowedHosts)
        if (authority == host) return true;
    return false;
}

inline bool validSha256(const std::string& s)
{
    if (s.size() != 64) return false;
    for (char c : s)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))  // lowercase only, by policy
            return false;
    return true;
}

// "1.6" -> (1 << 16) | 6, the NIB_ABI_VERSION packing from include/nib/nib.h.
inline bool parseAbi(const std::string& s, uint32_t& out)
{
    const size_t dot = s.find('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 >= s.size()) return false;
    unsigned long maj = 0, mnr = 0;
    for (size_t i = 0; i < dot; ++i) {
        const char c = s[i];
        if (c < '0' || c > '9') return false;
        maj = maj * 10 + static_cast<unsigned long>(c - '0');
        if (maj > 0xFFFF) return false;
    }
    for (size_t i = dot + 1; i < s.size(); ++i) {
        const char c = s[i];
        if (c < '0' || c > '9') return false;
        mnr = mnr * 10 + static_cast<unsigned long>(c - '0');
        if (mnr > 0xFFFF) return false;
    }
    out = static_cast<uint32_t>((maj << 16) | mnr);
    return true;
}

// Optional string member: absent is fine, present-with-wrong-type is an error (signed catalog =
// publishing bug, see the header comment). Returns false only on the type error.
inline bool takeStr(const wxnjson::Json& o, const char* key, std::string& dst, bool& present,
                    std::string& err)
{
    const wxnjson::Json* m = o.member(key);
    if (!m) { present = false; return true; }
    if (m->type != wxnjson::Json::Str) {
        err = std::string("\"") + key + "\" must be a string";
        return false;
    }
    present = true;
    dst = m->str;
    return true;
}

} // namespace detail

// --- index.json ----------------------------------------------------------------------------------

inline ParseResult parseIndex(const std::string& jsonText, Index& out)
{
    ParseResult r;
    out = Index();

    wxnjson::Json root;
    if (!wxnjson::JsonParser(jsonText).parse(root)) { r.error = "index: not valid JSON";         return r; }
    if (root.type != wxnjson::Json::Obj)            { r.error = "index: root is not an object";  return r; }

    // Range-check BEFORE the integer casts, the same order parseTargetList already uses for
    // install.size: converting an out-of-range double (JSON "1e400" arrives here as inf) to an
    // integer type is undefined behavior, not merely a wrong value.
    const wxnjson::Json* m = root.member("schema");
    if (!m || m->type != wxnjson::Json::Num || !(m->num >= 0 && m->num <= 1e6)) {
        r.error = "index: missing numeric \"schema\"";
        return r;
    }
    out.schema = static_cast<int>(m->num);
    if (out.schema != 1) { r.error = "index: unsupported \"schema\" version"; return r; }

    m = root.member("serial");
    // 2^53: the largest integer a double carries exactly - and more catalog publishes than can
    // ever happen under a monotonic serial.
    if (!m || m->type != wxnjson::Json::Num || !(m->num >= 0 && m->num <= 9007199254740992.0)) {
        r.error = "index: missing numeric \"serial\"";
        return r;
    }
    out.serial = static_cast<unsigned long long>(m->num);

    m = root.member("generated");
    if (m) {
        if (m->type != wxnjson::Json::Str) { r.error = "index: \"generated\" must be a string"; return r; }
        out.generated = m->str;
    }

    m = root.member("targets");
    if (m) {
        if (m->type != wxnjson::Json::Arr) { r.error = "index: \"targets\" must be an array"; return r; }
        for (const wxnjson::Json& t : m->arr) {
            if (t.type != wxnjson::Json::Str) { r.error = "index: \"targets\" must contain only strings"; return r; }
            out.targets.push_back(t.str);
        }
    }

    r.ok = true;
    return r;
}

// --- pl.<os>-<arch>.json -------------------------------------------------------------------------

inline ParseResult parseTargetList(const std::string& jsonText, std::vector<Entry>& out)
{
    ParseResult r;
    out.clear();

    wxnjson::Json root;
    if (!wxnjson::JsonParser(jsonText).parse(root)) { r.error = "target list: not valid JSON";        return r; }
    if (root.type != wxnjson::Json::Obj)            { r.error = "target list: root is not an object"; return r; }

    const wxnjson::Json* schema = root.member("schema");
    if (!schema || schema->type != wxnjson::Json::Num || static_cast<int>(schema->num) != 1) {
        r.error = "target list: missing or unsupported \"schema\"";
        return r;
    }

    const wxnjson::Json* entries = root.member("entries");
    if (!entries || entries->type != wxnjson::Json::Arr) {
        r.error = "target list: missing \"entries\" array";
        return r;
    }

    std::vector<Entry> parsed;
    parsed.reserve(entries->arr.size());

    for (size_t i = 0; i < entries->arr.size(); ++i) {
        const wxnjson::Json& je = entries->arr[i];

        // Every error below names the entry: by id when one is present (the useful case for the
        // catalog maintainer), by 1-based position when the id itself is missing or unusable.
        std::string label = "#" + std::to_string(i + 1);
        {
            const wxnjson::Json* jid = je.member("id");
            if (jid && jid->type == wxnjson::Json::Str && !jid->str.empty())
                label = "'" + jid->str + "'";
        }
        auto bad = [&](const std::string& what) {
            ParseResult f;
            f.error = "entry " + label + ": " + what;
            return f;
        };

        if (je.type != wxnjson::Json::Obj) return bad("not an object");

        Entry e;
        std::string ferr;
        bool present = false;

        // id / version / kind - required identity fields
        const wxnjson::Json* m = je.member("id");
        if (!m || m->type != wxnjson::Json::Str) return bad("missing \"id\"");
        e.id = m->str;
        if (!detail::validId(e.id)) return bad("\"id\" must be 1..128 chars of [a-z0-9.-]");

        m = je.member("version");
        if (!m || m->type != wxnjson::Json::Str) return bad("missing \"version\"");
        e.version = m->str;
        if (e.version.empty() || e.version.size() > 32) return bad("\"version\" must be 1..32 chars");

        m = je.member("kind");
        if (!m || m->type != wxnjson::Json::Str) return bad("missing \"kind\"");
        if      (m->str == "nib")        e.kind = Kind::Nib;
        else if (m->str == "npp-bridge") e.kind = Kind::NppBridge;
        else return bad("\"kind\" must be \"nib\" or \"npp-bridge\"");

        // display-only metadata - optional, but if present it must at least be the right type
        if (!detail::takeStr(je, "name",        e.name,        present, ferr)) return bad(ferr);
        if (!detail::takeStr(je, "description", e.description, present, ferr)) return bad(ferr);
        if (!detail::takeStr(je, "author",      e.author,      present, ferr)) return bad(ferr);
        if (!detail::takeStr(je, "homepage",    e.homepage,    present, ferr)) return bad(ferr);
        if (!detail::takeStr(je, "license",     e.licenseSpdx, present, ferr)) return bad(ferr);

        // ABI window. min-host-abi is REQUIRED: abiSatisfied's major-equality gate means a
        // defaulted minAbi of 0 can never match a real host, so an entry that omitted the field
        // was silently uninstallable everywhere - and in a curated, SIGNED catalog a missing bound
        // is a publishing bug to surface by name, not to guess around. max-host-abi stays
        // optional: absent means "no upper bound", which the open default already encodes.
        std::string abiText;
        if (!detail::takeStr(je, "min-host-abi", abiText, present, ferr)) return bad(ferr);
        if (!present) return bad("missing \"min-host-abi\"");
        if (!detail::parseAbi(abiText, e.minAbi))
            return bad("\"min-host-abi\" must look like \"1.6\"");
        if (!detail::takeStr(je, "max-host-abi", abiText, present, ferr)) return bad(ferr);
        if (present && !detail::parseAbi(abiText, e.maxAbi))
            return bad("\"max-host-abi\" must look like \"1.6\"");
        if (e.minAbi > e.maxAbi) return bad("abi range is inverted (min-host-abi > max-host-abi)");

        // install block - the part that touches the network and the filesystem, so every field is
        // required and every field is validated (see the validators' comments for the threat each
        // one closes).
        const wxnjson::Json* ji = je.member("install");
        if (!ji || ji->type != wxnjson::Json::Obj) return bad("missing \"install\" object");

        if (!detail::takeStr(*ji, "folder-name", e.install.folderName, present, ferr)) return bad(ferr);
        if (!present) return bad("missing \"install.folder-name\"");
        if (!detail::takeStr(*ji, "binary", e.install.binary, present, ferr)) return bad(ferr);
        if (!present) return bad("missing \"install.binary\"");
        if (!detail::takeStr(*ji, "package", e.install.packageUrl, present, ferr)) return bad(ferr);
        if (!present) return bad("missing \"install.package\"");
        if (!detail::takeStr(*ji, "sha256", e.install.sha256, present, ferr)) return bad(ferr);
        if (!present) return bad("missing \"install.sha256\"");

        m = ji->member("size");
        if (!m || m->type != wxnjson::Json::Num) return bad("missing numeric \"install.size\"");
        if (m->num < 1 || m->num > static_cast<double>(kMaxPackageSize))
            return bad("\"install.size\" out of range (1..209715200)");
        e.install.size = static_cast<unsigned long long>(m->num);
        if (static_cast<double>(e.install.size) != m->num)
            return bad("\"install.size\" must be an integer");

        if (!detail::validPathComponent(e.install.folderName))
            return bad("\"install.folder-name\" is not a safe path component");
        if (!detail::validPathComponent(e.install.binary))
            return bad("\"install.binary\" is not a safe path component");
        if (!detail::validPackageUrl(e.install.packageUrl))
            return bad("\"install.package\" must be an https URL on an allowlisted GitHub host");
        if (!detail::validSha256(e.install.sha256))
            return bad("\"install.sha256\" must be 64 lowercase hex chars");

        parsed.push_back(e);
    }

    out.swap(parsed);
    r.ok = true;
    return r;
}

// --- host-side predicates ------------------------------------------------------------------------

// This build's "<os>-<arch>" catalog target, decided entirely at compile time (deliberately NOT
// wxPlatformInfo - this header stays wx-free and the answer must match the CI matrix name the
// binary was actually built as). Exactly one of the 8 names in .github/workflows/build.yml.
inline const char* targetSlug()
{
#if defined(_WIN32)
#  if defined(_M_X64) || defined(__x86_64__)
    return "windows-x86_64";
#  elif defined(_M_ARM64) || defined(__aarch64__)
    return "windows-arm64";
#  elif defined(_M_IX86) || defined(__i386__)
    return "windows-x86";
#  else
#    error "unmapped Windows architecture - add it here AND to the CI matrix + catalog targets"
#  endif
#elif defined(__APPLE__)
#  if defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
    return "macos-arm64";
#  elif defined(__x86_64__)
    return "macos-x86_64";
#  else
#    error "unmapped macOS architecture - add it here AND to the CI matrix + catalog targets"
#  endif
#else   // the remaining CI legs are all Linux
#  if defined(__x86_64__)
    return "linux-x86_64";
#  elif defined(__aarch64__)
    return "linux-arm64";
#  elif defined(__riscv) && (__riscv_xlen == 64)
    return "linux-riscv64";
#  else
#    error "unmapped Linux architecture - add it here AND to the CI matrix + catalog targets"
#  endif
#endif
}

// ABI gate. NIB_ABI_VERSION packs (major << 16) | minor (include/nib/nib.h) and majors are
// incompatible by definition, so the entry's minimum must share the host's major AND the host must
// sit inside the entry's [minAbi, maxAbi] window (both ends inclusive).
inline bool abiSatisfied(const Entry& e, uint32_t hostAbi)
{
    if ((e.minAbi >> 16) != (hostAbi >> 16)) return false;
    return e.minAbi <= hostAbi && hostAbi <= e.maxAbi;
}

// "npp-bridge" entries are Notepad++-ABI plugins hosted by the GPL bridge. In practice that is a
// Windows reality (they are Win32 DLLs speaking NPPM_*/WM_*), but the gate here is deliberately the
// RUNTIME fact "is the bridge installed?", not a hardcoded platform check - the bridge plugin is
// the one thing that can or cannot host them, and it only exists where it can.
inline bool installable(const Entry& e, bool bridgePresent)
{
    if (e.kind == Kind::NppBridge) return bridgePresent;
    return true;
}

// Anti-rollback for the catalog itself: a replayed OLDER index could reintroduce a yanked or
// vulnerable plugin version as an "update" (the moved-tag half of the Sublime 2021 lesson). Equal
// serials are accepted - that is just re-fetching the catalog we already trust.
inline bool serialAcceptable(unsigned long long lastSeen, unsigned long long incoming)
{
    return incoming >= lastSeen;
}

} // namespace wxnplug
