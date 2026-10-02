// SPDX-License-Identifier: GPL-3.0-or-later
//
// npp-compat - a small XML reader for Notepad++'s own files (see npp_xml.h).
// Copyright 2026 The wxNote Authors. See LICENSE (GPL-3.0-or-later).

#include "npp_xml.h"

#include <cstring>

namespace nppcompat {

namespace {

const size_t kMaxDepth      = 64;
const size_t kMaxElements   = 500000;       // across the file; Notepad++'s largest hold a few thousand
const size_t kMaxAttributes = 1000000;      // likewise: together they bound the memory a hostile file costs
const size_t kMaxInput      = 64u << 20;   // 64 MiB: Notepad++'s own files are kilobytes

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
bool isNameChar(char c) { return !isSpace(c) && c != '=' && c != '/' && c != '>' && c != '<' && c != '"' && c != '\''; }

void appendUtf8(std::string& out, unsigned long cp)
{
    if (cp < 0x80) out += static_cast<char>(cp);
    else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000)
    {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
    else
    {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

// Decode the five predefined entities and numeric character references; keep anything else literal.
// No entity is ever looked up anywhere else - that is the whole XXE posture.
std::string decode(const char* p, size_t n)
{
    std::string out;
    out.reserve(n);
    for (size_t i = 0; i < n;)
    {
        if (p[i] != '&') { out += p[i++]; continue; }
        size_t semi = i + 1;
        while (semi < n && semi - i <= 12 && p[semi] != ';') ++semi;
        if (semi >= n || p[semi] != ';') { out += p[i++]; continue; }
        const std::string ent(p + i + 1, semi - i - 1);
        if (ent == "lt") out += '<';
        else if (ent == "gt") out += '>';
        else if (ent == "amp") out += '&';
        else if (ent == "quot") out += '"';
        else if (ent == "apos") out += '\'';
        else if (ent.size() > 1 && ent[0] == '#')
        {
            const bool hex = ent[1] == 'x' || ent[1] == 'X';
            unsigned long cp = 0;
            bool ok = ent.size() > (hex ? 2u : 1u);
            for (size_t k = hex ? 2 : 1; ok && k < ent.size(); ++k)
            {
                const char c = ent[k];
                unsigned d;
                if (c >= '0' && c <= '9') d = static_cast<unsigned>(c - '0');
                else if (hex && c >= 'a' && c <= 'f') d = static_cast<unsigned>(c - 'a' + 10);
                else if (hex && c >= 'A' && c <= 'F') d = static_cast<unsigned>(c - 'A' + 10);
                else { ok = false; break; }
                cp = cp * (hex ? 16 : 10) + d;
                if (cp > 0x10FFFF) ok = false;
            }
            if (ok && cp != 0 && !(cp >= 0xD800 && cp <= 0xDFFF)) appendUtf8(out, cp);
            else out += "\xEF\xBF\xBD";
        }
        else { out += '&'; out += ent; out += ';'; }
        i = semi + 1;
    }
    return out;
}

}   // namespace

std::string XmlElement::attr(const char* key, const std::string& def) const
{
    for (const auto& a : attrs)
        if (a.first == key) return a.second;
    return def;
}

bool XmlElement::hasAttr(const char* key) const
{
    for (const auto& a : attrs)
        if (a.first == key) return true;
    return false;
}

const XmlElement* XmlElement::child(const char* elementName) const
{
    for (const XmlElement& c : children)
        if (c.name == elementName) return &c;
    return nullptr;
}

bool parseXml(const std::string& in, XmlElement& root, std::string* err)
{
    auto fail = [err](const char* why) { if (err) *err = why; return false; };
    root = XmlElement();
    if (in.size() > kMaxInput) return fail("too large for a Notepad++ file");
    const char* s = in.data();
    size_t n = in.size(), i = 0;
    if (n >= 3 && std::memcmp(s, "\xEF\xBB\xBF", 3) == 0) i = 3;   // UTF-8 byte-order mark

    std::vector<XmlElement*> open;      // the elements not yet closed; open.back() receives what comes next
    bool haveRoot = false;
    size_t elements = 0, attributes = 0;
    while (i < n)
    {
        if (s[i] != '<')
        {
            const size_t lt = in.find('<', i);
            const size_t end = lt == std::string::npos ? n : lt;
            if (!open.empty()) open.back()->text += decode(s + i, end - i);
            else
                for (size_t k = i; k < end; ++k)
                    if (!isSpace(s[k])) return fail("text outside the root element");
            i = end;
            continue;
        }
        if (in.compare(i, 4, "<!--") == 0)
        {
            const size_t e = in.find("-->", i + 4);
            if (e == std::string::npos) return fail("unterminated comment");
            i = e + 3;
            continue;
        }
        if (in.compare(i, 9, "<![CDATA[") == 0)
        {
            const size_t e = in.find("]]>", i + 9);
            if (e == std::string::npos) return fail("unterminated CDATA section");
            if (open.empty()) return fail("text outside the root element");
            open.back()->text.append(s + i + 9, e - i - 9);
            i = e + 3;
            continue;
        }
        if (in.compare(i, 2, "<?") == 0)
        {
            const size_t e = in.find("?>", i + 2);
            if (e == std::string::npos) return fail("unterminated processing instruction");
            i = e + 2;
            continue;
        }
        if (in.compare(i, 2, "<!") == 0)                     // DOCTYPE and its internal subset: skipped whole
        {
            size_t k = i + 2;
            int bracket = 0;
            while (k < n && !(s[k] == '>' && bracket == 0))
            {
                if (s[k] == '[') ++bracket;
                else if (s[k] == ']' && bracket > 0) --bracket;
                ++k;
            }
            if (k >= n) return fail("unterminated declaration");
            i = k + 1;
            continue;
        }
        if (in.compare(i, 2, "</") == 0)
        {
            size_t k = i + 2;
            while (k < n && isNameChar(s[k])) ++k;
            const std::string name(s + i + 2, k - i - 2);
            while (k < n && isSpace(s[k])) ++k;
            if (k >= n || s[k] != '>') return fail("malformed closing tag");
            if (open.empty() || open.back()->name != name) return fail("mismatched closing tag");
            open.pop_back();
            i = k + 1;
            continue;
        }

        // An element: <name attr="value" ...> or <name ... />
        size_t k = i + 1;
        while (k < n && isNameChar(s[k])) ++k;
        if (k == i + 1) return fail("malformed tag");
        XmlElement el;
        el.name.assign(s + i + 1, k - i - 1);
        bool selfClose = false;
        for (;;)
        {
            while (k < n && isSpace(s[k])) ++k;
            if (k >= n) return fail("unterminated tag");
            if (s[k] == '>') { ++k; break; }
            if (s[k] == '/' && k + 1 < n && s[k + 1] == '>') { selfClose = true; k += 2; break; }
            const size_t a = k;
            while (k < n && isNameChar(s[k])) ++k;
            if (k == a) return fail("malformed attribute");
            std::string key(s + a, k - a);
            while (k < n && isSpace(s[k])) ++k;
            std::string value;
            if (k < n && s[k] == '=')
            {
                ++k;
                while (k < n && isSpace(s[k])) ++k;
                if (k >= n || (s[k] != '"' && s[k] != '\'')) return fail("unquoted attribute value");
                const char q = s[k++];
                const size_t e = in.find(q, k);
                if (e == std::string::npos) return fail("unterminated attribute value");
                value = decode(s + k, e - k);
                k = e + 1;
            }
            if (++attributes > kMaxAttributes) return fail("too many attributes");
            el.attrs.emplace_back(std::move(key), std::move(value));
        }
        if (++elements > kMaxElements) return fail("too many elements");
        XmlElement* placed;
        if (open.empty())
        {
            if (haveRoot) return fail("a second root element");
            root = std::move(el);
            haveRoot = true;
            placed = &root;
        }
        else
        {
            open.back()->children.push_back(std::move(el));
            placed = &open.back()->children.back();
        }
        if (!selfClose)
        {
            if (open.size() >= kMaxDepth) return fail("nested too deeply");
            open.push_back(placed);
        }
        i = k;
    }
    if (!open.empty()) return fail("unclosed element");
    if (!haveRoot) return fail("no root element");
    return true;
}

std::string leadingXmlComment(const std::string& text)
{
    size_t i = 0;
    for (;;)
    {
        i = text.find('<', i);
        if (i == std::string::npos) return std::string();
        if (text.compare(i, 4, "<!--") == 0)
        {
            const size_t e = text.find("-->", i + 4);
            return e == std::string::npos ? std::string() : text.substr(i + 4, e - i - 4);
        }
        if (text.compare(i, 2, "<?") != 0 && text.compare(i, 2, "<!") != 0) return std::string();   // the root came first
        ++i;
    }
}

}   // namespace nppcompat
