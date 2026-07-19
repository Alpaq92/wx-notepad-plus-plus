// SPDX-License-Identifier: GPL-3.0-or-later
//
// npp-shortcuts-compat - parse a Notepad++ shortcuts.xml into a NppShortcutsDoc.
// Copyright 2026 The wxNote Authors. See LICENSE (GPL-3.0-or-later).

#include "npp_shortcuts_parse.h"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace nppcompat {
namespace {

// XML entity unescape for the five predefined entities plus decimal/hex numeric character
// references (ASCII range only). Deliberately does NOT expand any other "&name;" (no DTD / no
// custom or external entities): an unknown entity is kept literal. This is the plugin's whole
// XXE posture - there is no code path that dereferences a declared or external entity.
std::string unescape(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        if (s[i] != '&') { out += s[i++]; continue; }
        const size_t semi = s.find(';', i);
        if (semi == std::string::npos || semi - i > 12) { out += s[i++]; continue; }  // bounded lookahead
        const std::string ent = s.substr(i + 1, semi - i - 1);
        if (ent == "lt") out += '<';
        else if (ent == "gt") out += '>';
        else if (ent == "amp") out += '&';
        else if (ent == "quot") out += '"';
        else if (ent == "apos") out += '\'';
        else if (!ent.empty() && ent[0] == '#') {
            const long cp = (ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X'))
                                ? std::strtol(ent.c_str() + 2, nullptr, 16)
                                : std::strtol(ent.c_str() + 1, nullptr, 10);
            if (cp > 0 && cp < 128) out += static_cast<char>(cp);   // ASCII only; drop NUL and non-ASCII
        } else { out += '&'; out += ent; out += ';'; }              // unknown entity: keep literal
        i = semi + 1;
    }
    return out;
}

// Value of attribute `name` within the tag body `tag` (text between '<' and '>'). Handles single or
// double quotes; requires `name` to be a whole attribute token. Returns "" if absent. (Same shape as
// udl-compat's attr(): tolerant of the real, hand-edited files without pulling in an XML library.)
std::string attr(const std::string& tag, const std::string& name)
{
    size_t p = 0;
    while ((p = tag.find(name, p)) != std::string::npos) {
        const char before = p == 0 ? ' ' : tag[p - 1];
        p += name.size();
        size_t q = p;
        while (q < tag.size() && std::isspace((unsigned char)tag[q])) ++q;
        if ((before == ' ' || std::isspace((unsigned char)before)) && q < tag.size() && tag[q] == '=') {
            ++q;
            while (q < tag.size() && std::isspace((unsigned char)tag[q])) ++q;
            if (q < tag.size() && (tag[q] == '"' || tag[q] == '\'')) {
                const char quote = tag[q++];
                const size_t end = tag.find(quote, q);
                if (end != std::string::npos) return unescape(tag.substr(q, end - q));
            }
        }
    }
    return "";
}

bool boolAttr(const std::string& tag, const std::string& name)
{
    std::string v = attr(tag, name);
    for (char& c : v) c = (char)std::tolower((unsigned char)c);
    return v == "yes" || v == "1" || v == "true";
}

// Integer attribute, clamped to [lo, hi]. Untrusted input: anything non-numeric or out of range
// collapses to `def`. strtol stops at the first non-digit, so an injected "83; rm -rf" yields 83.
int intAttr(const std::string& tag, const std::string& name, int def, int lo, int hi)
{
    const std::string v = attr(tag, name);
    if (v.empty()) return def;
    char* endp = nullptr;
    const long n = std::strtol(v.c_str(), &endp, 10);
    if (endp == v.c_str()) return def;                 // no digits at all
    if (n < lo || n > hi) return def;
    return (int)n;
}

// A VK is a single byte in Windows; clamp to [0,255]. Out-of-range -> 0 (treated as "no key").
int vkAttr(const std::string& tag) { return intAttr(tag, "Key", 0, 0, 255); }

NppKey keyFrom(const std::string& tag)
{
    NppKey k;
    k.ctrl  = boolAttr(tag, "Ctrl");
    k.alt   = boolAttr(tag, "Alt");
    k.shift = boolAttr(tag, "Shift");
    k.vk    = vkAttr(tag);
    return k;
}

// Remove XML comments and any DOCTYPE / processing-instruction declarations before scanning. This is
// belt-and-suspenders XXE hardening: we never expand entities anyway, but stripping the DTD means a
// planted <!ENTITY>/<!DOCTYPE> can neither be parsed nor accidentally matched as content, and stripping
// comments stops a "<Shortcut" inside a comment from being read as a real binding.
std::string stripNonContent(const std::string& in)
{
    // SINGLE forward pass, copying kept spans into `out`. The previous version restarted find() from
    // offset 0 and erase()d from the middle on every comment/DOCTYPE, which is O(n^2) on hostile input:
    // a large file followed by many tiny comments rescanned the multi-MB prefix per comment (~minutes of
    // freeze). Since the auto-probed %APPDATA%\Notepad++\shortcuts.xml can be planted by a third party,
    // that is a denial-of-service the parser must not have. Scanning once and skipping the stripped
    // regions is O(n) with no repeated rescans.
    std::string out;
    out.reserve(in.size());
    const size_t n = in.size();
    size_t i = 0;
    while (i < n) {
        // Comment: <!-- ... -->
        if (in.compare(i, 4, "<!--") == 0) {
            const size_t e = in.find("-->", i + 4);
            if (e == std::string::npos) break;   // unterminated: drop the tail
            i = e + 3;
            continue;
        }
        // DOCTYPE with an optional [ internal subset ]. (Only <!DOCTYPE is stripped, matching the prior
        // behaviour; plain <? ?> PIs and generic <! > were never removed here.)
        if (in.compare(i, 9, "<!DOCTYPE") == 0) {
            size_t gt = in.find('>', i + 9);
            const size_t br = in.find('[', i + 9);
            if (br != std::string::npos && (gt == std::string::npos || br < gt)) {
                const size_t endbr = in.find(']', br);
                gt = (endbr == std::string::npos) ? std::string::npos : in.find('>', endbr);
            }
            if (gt == std::string::npos) break;   // unterminated: drop the tail
            i = gt + 1;
            continue;
        }
        out += in[i++];
    }
    return out;
}

// Find the next "<name" element-open whose name is a whole token (followed by whitespace, '>' or '/').
// Returns npos if none. On success sets tagBody to the text between '<' and '>' and past to the index
// just after '>'; selfClose is true for "<name ... />".
size_t findElement(const std::string& xml, const char* name, size_t from,
                   std::string& tagBody, bool& selfClose, size_t& past)
{
    const std::string open = std::string("<") + name;
    size_t p = from;
    while ((p = xml.find(open, p)) != std::string::npos) {
        const size_t after = p + open.size();
        const char c = after < xml.size() ? xml[after] : '\0';
        if (c == '\0' || std::isspace((unsigned char)c) || c == '>' || c == '/') {
            const size_t gt = xml.find('>', after);
            if (gt == std::string::npos) return std::string::npos;
            tagBody   = xml.substr(p + 1, gt - p - 1);            // between '<' and '>', name included
            selfClose = (gt > p && xml[gt - 1] == '/');
            past      = gt + 1;
            return p;
        }
        p = after;   // "<Shortcuts" etc. - not our element; keep looking
    }
    return std::string::npos;
}

}  // namespace

bool parseShortcutsXml(const std::string& rawXml, NppShortcutsDoc& out, std::string* err)
{
    out = NppShortcutsDoc{};
    const std::string xml = stripNonContent(rawXml);

    out.foundRoot = xml.find("<NotepadPlus") != std::string::npos ||
                    xml.find("<wxNote")      != std::string::npos;
    if (!out.foundRoot) { if (err) *err = "no <NotepadPlus> (or <wxNote>) root element"; return false; }

    std::string tag; bool self = false; size_t past = 0, p = 0;

    // <Shortcut id=... Ctrl Alt Shift Key [nth]/> (InternalCommands). Self-closing in real files.
    for (p = 0; (p = findElement(xml, "Shortcut", p, tag, self, past)) != std::string::npos; p = past) {
        NppInternal it;
        it.cmdId = intAttr(tag, "id", -1, 0, 200000);
        if (it.cmdId < 0) continue;                     // an entry with no usable id is dropped
        it.key   = keyFrom(tag);
        it.nth   = intAttr(tag, "nth", 0, 0, 64);
        out.internals.push_back(std::move(it));
    }

    // <ScintKey ScintID=... Ctrl Alt Shift Key> <NextKey .../>* </ScintKey> (ScintillaKeys).
    for (p = 0; (p = findElement(xml, "ScintKey", p, tag, self, past)) != std::string::npos; p = past) {
        NppScintKey sk;
        sk.sciId = intAttr(tag, "ScintID", -1, 0, 100000);
        if (sk.sciId < 0) continue;
        sk.keys.push_back(keyFrom(tag));                // the primary key lives on the ScintKey element
        if (!self) {
            const size_t close = xml.find("</ScintKey>", past);
            const size_t bodyEnd = (close == std::string::npos) ? xml.size() : close;
            std::string nkTag; bool nkSelf = false; size_t nkPast = 0;
            for (size_t q = past; (q = findElement(xml, "NextKey", q, nkTag, nkSelf, nkPast)) != std::string::npos
                                  && q < bodyEnd; q = nkPast)
                sk.keys.push_back(keyFrom(nkTag));      // each <NextKey/> = an additional binding (N++ NextKey)
            if (close != std::string::npos) past = close + 11;   // 11 = strlen("</ScintKey>")
        }
        out.scintKeys.push_back(std::move(sk));
    }

    // <PluginCommand moduleName=... internalID=... Ctrl Alt Shift Key/> (PluginCommands).
    for (p = 0; (p = findElement(xml, "PluginCommand", p, tag, self, past)) != std::string::npos; p = past) {
        NppPluginCmd pc;
        pc.moduleName = attr(tag, "moduleName");
        pc.internalId = intAttr(tag, "internalID", 0, 0, 100000);
        pc.key        = keyFrom(tag);
        out.pluginCmds.push_back(std::move(pc));
    }

    // <Command name=... Ctrl Alt Shift Key>SHELL COMMAND LINE</Command> (UserDefinedCommands).
    // The element text is a shell command line captured as OPAQUE DATA - never executed (see header).
    for (p = 0; (p = findElement(xml, "Command", p, tag, self, past)) != std::string::npos; p = past) {
        NppUserCmd uc;
        uc.name = attr(tag, "name");
        uc.key  = keyFrom(tag);
        if (!self) {
            const size_t close = xml.find("</Command>", past);
            if (close != std::string::npos) {
                uc.commandLine = unescape(xml.substr(past, close - past));
                past = close + 10;                       // 10 = strlen("</Command>")
            }
        }
        out.userCmds.push_back(std::move(uc));
    }

    // <Macro name=... Ctrl Alt Shift Key> <Action .../>* </Macro> (Macros). Actions counted, not replayed.
    for (p = 0; (p = findElement(xml, "Macro", p, tag, self, past)) != std::string::npos; p = past) {
        NppMacro m;
        m.name = attr(tag, "name");
        m.key  = keyFrom(tag);
        if (!self) {
            const size_t close = xml.find("</Macro>", past);
            const size_t bodyEnd = (close == std::string::npos) ? xml.size() : close;
            std::string aTag; bool aSelf = false; size_t aPast = 0;
            for (size_t q = past; (q = findElement(xml, "Action", q, aTag, aSelf, aPast)) != std::string::npos
                                  && q < bodyEnd; q = aPast)
                ++m.actionCount;
            if (close != std::string::npos) past = close + 8;   // 8 = strlen("</Macro>")
        }
        out.macros.push_back(std::move(m));
    }

    return true;
}

std::string readFileToString(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) return "";
    // Cap the slurp. A real shortcuts.xml is a few KB; refuse anything absurd so a planted multi-GB or
    // comment-bomb file at the auto-probed %APPDATA%\Notepad++\shortcuts.xml cannot OOM the import (the
    // whole file is held in memory and stripNonContent copies it once more). 8 MiB is orders of
    // magnitude above any legitimate keymap file.
    constexpr std::streamoff kMaxBytes = 8 * 1024 * 1024;
    in.seekg(0, std::ios::end);
    const std::streamoff sz = in.tellg();
    if (sz <= 0 || sz > kMaxBytes) return "";   // unseekable/empty/oversized: treat as "no file"
    in.seekg(0, std::ios::beg);
    std::string s(static_cast<size_t>(sz), '\0');
    in.read(&s[0], sz);
    s.resize(static_cast<size_t>(in.gcount()));   // short read (e.g. truncated): keep only what we got
    return s;
}

std::string formatReport(const std::string& sourcePath, const NppShortcutsDoc& doc,
                         const ImportTally& tally)
{
    std::ostringstream r;
    r << "Notepad++ shortcuts.xml import\n";
    r << "==============================\n";
    r << "Source: " << (sourcePath.empty() ? "(none)" : sourcePath) << "\n\n";

    r << "Sections found:\n";
    r << "  Internal commands : " << doc.internals.size()  << "\n";
    r << "  Scintilla keys    : " << doc.scintKeys.size()  << "\n";
    r << "  Plugin commands   : " << doc.pluginCmds.size() << "\n";
    r << "  User commands     : " << doc.userCmds.size()
      << "   (shown for review only - NEVER executed)\n";
    r << "  Macros            : " << doc.macros.size()
      << "   (shown for review only - NEVER replayed)\n\n";

    r << "Result:\n";
    r << "  Imported : " << tally.imported << " binding(s)\n";
    r << "  Unmapped : " << tally.unmapped << " (key had no portable equivalent)\n";
    r << "  Unknown  : " << tally.unknown  << " (command / editor id not recognised by this build)\n\n";

    if (!tally.unmappedNotes.empty()) {
        r << "Unmapped keys:\n";
        for (const std::string& s : tally.unmappedNotes) r << "  - " << s << "\n";
        r << "\n";
    }
    if (!tally.unknownNotes.empty()) {
        r << "Unknown targets:\n";
        for (const std::string& s : tally.unknownNotes) r << "  - " << s << "\n";
        r << "\n";
    }

    // The security stance, made visible: user commands and macros are surfaced verbatim so the user can
    // review them, and flagged as NOT run. This plugin has no execution path for either.
    if (!doc.userCmds.empty() || !doc.macros.empty()) {
        r << "Shown for review only - wxNote has no path that runs any of these:\n";
        for (const NppUserCmd& uc : doc.userCmds)
            r << "  User command \"" << uc.name << "\"  ->  " << uc.commandLine
              << "   [NOT EXECUTED]\n";
        for (const NppMacro& m : doc.macros)
            r << "  Macro \"" << m.name << "\"  (" << m.actionCount << " action(s))   [NOT REPLAYED]\n";
        r << "\n";
    }

    r << "The imported bindings were registered as the scheme \"Notepad++ (imported)\".\n";
    r << "Open the Shortcut Mapper's scheme picker and select it there to apply them.\n";
    return r.str();
}

}  // namespace nppcompat
