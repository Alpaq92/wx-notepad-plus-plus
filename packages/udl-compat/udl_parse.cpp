// SPDX-License-Identifier: GPL-3.0-or-later
//
// udl-compat - parse a Notepad++ userDefineLang.xml into a UdlDef.
// Copyright 2026 The wxNote Authors. See LICENSE (GPL-3.0-or-later).

#include "udl_parse.h"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace udlcompat {
namespace {

// XML entity unescape for the five predefined entities plus decimal/hex numeric
// character references (ASCII range only - enough for UDL token text).
std::string unescape(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        if (s[i] != '&') { out += s[i++]; continue; }
        const size_t semi = s.find(';', i);
        if (semi == std::string::npos) { out += s[i++]; continue; }
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
            if (cp > 0 && cp < 128) out += static_cast<char>(cp);
        } else { out += '&'; out += ent; out += ';'; }   // unknown entity: keep literal
        i = semi + 1;
    }
    return out;
}

// Value of attribute `name` within the tag body `tag` (text between '<' and '>').
// Handles single or double quotes. Returns "" if absent.
std::string attr(const std::string& tag, const std::string& name)
{
    size_t p = 0;
    while ((p = tag.find(name, p)) != std::string::npos) {
        const size_t before = p == 0 ? ' ' : tag[p - 1];
        p += name.size();
        // require name to be a whole attribute token (preceded by space, followed by '=')
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

std::vector<std::string> splitWhitespace(const std::string& s)
{
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && std::isspace((unsigned char)s[i])) ++i;
        const size_t start = i;
        while (i < s.size() && !std::isspace((unsigned char)s[i])) ++i;
        if (i > start) out.push_back(s.substr(start, i - start));
    }
    return out;
}

// Decode the packed "Comments" list exactly like src/udl.h: 00=line-open,
// 01=block-open, 02=block-close (index-prefixed, space-separated tokens).
void decodeComments(const std::string& packed, UdlDef& out)
{
    for (const std::string& t : splitWhitespace(packed)) {
        if (t.size() < 2) continue;
        const std::string idx = t.substr(0, 2), val = t.substr(2);
        if (idx == "00") out.lineComment = val;
        else if (idx == "01") out.blockCommentOpen = val;
        else if (idx == "02") out.blockCommentClose = val;
    }
}

// Decode the packed "Delimiters" list exactly like src/udl.h: token index/3 = set,
// index%3 = slot (0=open, 1=escape, 2=close). Emits one UdlDef::Delimiter per set
// that has a non-empty open token.
void decodeDelimiters(const std::string& packed, UdlDef& out)
{
    UdlDef::Delimiter sets[8];
    for (const std::string& t : splitWhitespace(packed)) {
        if (t.size() < 2) continue;
        char* endp = nullptr;
        const long idx = std::strtol(t.substr(0, 2).c_str(), &endp, 10);
        if (idx < 0 || idx >= 24) continue;
        const std::string val = t.substr(2);
        const int set = static_cast<int>(idx) / 3, slot = static_cast<int>(idx) % 3;
        if (slot == 0) sets[set].open = val;
        else if (slot == 1) sets[set].escape = val;
        else sets[set].close = val;
    }
    for (const auto& d : sets)
        if (!d.open.empty()) out.delimiters.push_back(d);
}

}  // namespace

namespace {

// Parse one <UserLang> element (whose "<UserLang" begins at `ul`) into `out`. Sets `nextPos` to the
// index just past its </UserLang>, or npos if this element runs to EOF (so no more can follow).
bool parseOneUserLang(const std::string& xml, size_t ul, UdlDef& out, size_t& nextPos, std::string* err)
{
    out = UdlDef{};
    nextPos = std::string::npos;
    const size_t tagEnd = xml.find('>', ul);
    if (tagEnd == std::string::npos) { if (err) *err = "malformed <UserLang> tag"; return false; }
    const std::string openTag = xml.substr(ul, tagEnd - ul);

    // Compute this element's extent + nextPos BEFORE validating its content: a skipped element (e.g.
    // one with no name) must still advance the multi-language loop past its </UserLang>, otherwise a
    // single nameless <UserLang> would abort parsing and silently drop every later sibling.
    const size_t close = xml.find("</UserLang>", tagEnd);
    const std::string body = xml.substr(tagEnd + 1,
        (close == std::string::npos ? xml.size() : close) - tagEnd - 1);
    nextPos = (close == std::string::npos) ? std::string::npos : close + 11;   // 11 = strlen("</UserLang>")

    out.name = attr(openTag, "name");
    out.ext = attr(openTag, "ext");
    out.keywordsCaseInsensitive = false;
    if (out.name.empty()) { if (err) *err = "<UserLang> has no name"; return false; }

    // <Global caseIgnored="yes"/>
    {
        const size_t g = body.find("<Global");
        if (g != std::string::npos) {
            const size_t ge = body.find('>', g);
            if (ge != std::string::npos) {
                const std::string ci = attr(body.substr(g, ge - g), "caseIgnored");
                out.keywordsCaseInsensitive = (ci == "yes" || ci == "1");
            }
        }
    }

    // <Prefix Keywords1="yes" ... Keywords8="no" /> : per-group "prefix mode" (words match as prefixes).
    {
        const size_t pf = body.find("<Prefix");
        if (pf != std::string::npos) {
            const size_t pe = body.find('>', pf);
            if (pe != std::string::npos) {
                const std::string tag = body.substr(pf, pe - pf);
                for (int i = 0; i < 8; ++i) {
                    const std::string v = attr(tag, "Keywords" + std::to_string(i + 1));
                    out.keywordPrefix[i] = (v == "yes" || v == "1");
                }
            }
        }
    }

    // Walk every <Keywords name="...">text</Keywords>.
    size_t p = 0;
    while ((p = body.find("<Keywords", p)) != std::string::npos) {
        const size_t te = body.find('>', p);
        if (te == std::string::npos) break;
        const std::string kwTag = body.substr(p, te - p);
        const std::string name = attr(kwTag, "name");
        const size_t ce = body.find("</Keywords>", te);
        const std::string text = (ce == std::string::npos) ? "" : unescape(body.substr(te + 1, ce - te - 1));
        p = (ce == std::string::npos) ? te + 1 : ce + 11;

        if (name.rfind("Keywords", 0) == 0 && name.size() == 9 && name[8] >= '1' && name[8] <= '8') {
            out.keywords[name[8] - '1'] = splitWhitespace(text);
        } else if (name == "Comments") {
            decodeComments(text, out);
        } else if (name == "Delimiters") {
            decodeDelimiters(text, out);
        } else if (name == "Operators1") {
            std::string ops;
            for (const std::string& op : splitWhitespace(text))
                for (char c : op)
                    if (ops.find(c) == std::string::npos) ops += c;   // single-char operators, deduped
            out.operators = ops;
        }
        // Fold keywords: N++ stores them as "Folders in code1/2, open|middle|close" and
        // "Folders in comment, open|middle|close". Space-separated literal lists (not styled here;
        // the translator emits Scintillua fold points from them).
        else if (name == "Folders in code1, open")    out.foldCode1.open    = splitWhitespace(text);
        else if (name == "Folders in code1, middle")  out.foldCode1.middle  = splitWhitespace(text);
        else if (name == "Folders in code1, close")   out.foldCode1.close   = splitWhitespace(text);
        else if (name == "Folders in code2, open")    out.foldCode2.open    = splitWhitespace(text);
        else if (name == "Folders in code2, middle")  out.foldCode2.middle  = splitWhitespace(text);
        else if (name == "Folders in code2, close")   out.foldCode2.close   = splitWhitespace(text);
        else if (name == "Folders in comment, open")  out.foldComment.open  = splitWhitespace(text);
        else if (name == "Folders in comment, middle")out.foldComment.middle= splitWhitespace(text);
        else if (name == "Folders in comment, close") out.foldComment.close = splitWhitespace(text);
    }

    out.hasNumbers = true;   // UDL languages always style numbers
    return true;
}

}  // namespace

bool parseUserDefineLangXml(const std::string& xml, UdlDef& out, std::string* err)
{
    const size_t ul = xml.find("<UserLang");
    if (ul == std::string::npos) { if (err) *err = "no <UserLang> element"; return false; }
    size_t next = std::string::npos;
    return parseOneUserLang(xml, ul, out, next, err);
}

std::vector<UdlDef> parseAllUserDefineLangs(const std::string& xml)
{
    // Notepad++'s main userDefineLang.xml bundles every user language as a sibling <UserLang> under
    // <NotepadPlus>; iterate them all (parseUserDefineLangXml only returns the first, for callers that
    // want a single definition). A malformed or nameless element is skipped, not fatal to the rest.
    std::vector<UdlDef> all;
    size_t pos = 0;
    while ((pos = xml.find("<UserLang", pos)) != std::string::npos) {
        UdlDef d;
        size_t next = std::string::npos;
        const bool ok = parseOneUserLang(xml, pos, d, next, nullptr);
        if (ok) all.push_back(std::move(d));
        if (next == std::string::npos) break;      // this element ran to EOF - nothing can follow
        pos = (next > pos) ? next : pos + 9;        // advance past </UserLang> (or the tag, to make progress)
    }
    return all;
}

std::string readFileToString(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) return "";
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

}  // namespace udlcompat
