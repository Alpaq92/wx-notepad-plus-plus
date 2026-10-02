// SPDX-License-Identifier: Apache-2.0
//
// wxNote - yaml_io.h: reading and writing wxNote's own files (YAML) and the Plugins Admin catalog (JSON).
// Copyright 2026 The wxNote Authors.
//
// One thin layer over rapidyaml (MIT; fetched and pinned in CMakeLists.txt, compiled once in
// src/ryml_impl.cpp), so the stores above it - settings, key bindings, themes, sessions - share one set
// of rules instead of each growing its own:
//
//   * Nothing here throws. The build turns rapidyaml's errors into exceptions; parse() and parseJson()
//     catch them and report "line N, column M: message" in Doc::error. A file that does not parse
//     leaves its caller on its defaults - a bad hand edit must not brick startup.
//   * Values are read as what the caller expects, not as what YAML guesses: getText(), getInteger(),
//     getNumber() and getBool() take the scalar that is there and say whether it fits. A null
//     (`key:`, `key: ~`, `key: null`) reads as absent.
//   * Anchors and aliases are not followed: an alias reads as absent. Expanding them is how a small
//     file becomes gigabytes ("billion laughs"), and themes are imported from anywhere.
//   * What setText(), setInteger() and setBool() write reads back as the same value in any YAML
//     parser: text that would otherwise read as a null, a bool or a number is quoted, and text holding
//     control characters is double-quoted with escapes.
//
// No wx: the stores convert at their edges, and the headless tests link nothing else.

#pragma once

#include "ryml_all.hpp"

#include <cstddef>
#include <exception>
#include <string>

namespace wxnyaml {

using Node    = ryml::ConstNodeRef;     // a node of a parsed document (read side)
using MutNode = ryml::NodeRef;          // a node of a tree being built (write side)

inline ryml::csubstr view(const std::string& s) { return ryml::csubstr(s.data(), s.size()); }

// ---- parsing ---------------------------------------------------------------------------------------

struct Doc
{
    ryml::Tree  tree;
    std::string error;                  // empty when the text parsed

    bool ok() const { return error.empty(); }

    // The document's top node; not readable() for a failed parse or one that found nothing (empty
    // text, only comments). A file with an explicit "---" parses into a stream of documents, of which
    // only the first counts.
    Node root() const
    {
        if (!error.empty() || tree.size() == 0) return Node();
        Node r = tree.crootref();
        if (r.is_stream()) r = r.has_children() ? r.first_child() : Node();
        if (r.readable() && !r.is_container() && (!r.has_val() || r.val_is_null())) return Node();
        return r;
    }
};

namespace detail {

template <class ParseFn>
bool parseWith(ParseFn parseFn, const std::string& text, Doc& out, const std::string& name)
{
    out.tree.clear();
    out.tree.clear_arena();
    out.error.clear();
    try
    {
        parseFn(view(name), view(text), &out.tree);
        return true;
    }
    catch (const ryml::ExceptionParse& e)
    {
        const ryml::Location& at = e.errdata_parse.ymlloc;
        const size_t none = static_cast<size_t>(-1);
        if (at.line != none)
            out.error = "line " + std::to_string(at.line)
                      + (at.col != none ? ", column " + std::to_string(at.col) : std::string()) + ": ";
        out.error += e.msg;
    }
    catch (const std::exception& e)
    {
        out.error = e.what();
    }
    catch (...)
    {
    }
    if (out.error.empty()) out.error = "not readable";   // a failure must never read as success
    out.tree.clear();
    out.tree.clear_arena();
    return false;
}

}   // namespace detail

// Parse YAML text into `out`. `name` (a file name) only labels rapidyaml's own messages. On failure
// returns false, leaves out.tree empty and puts the reason in out.error.
inline bool parse(const std::string& text, Doc& out, const std::string& name = std::string())
{
    return detail::parseWith([](ryml::csubstr n, ryml::csubstr src, ryml::Tree* t) { ryml::parse_in_arena(n, src, t); },
                             text, out, name);
}

// Parse JSON. Strings stay told apart from numbers and literals (jsonType() below), which YAML's own
// reading of the same text would blur.
inline bool parseJson(const std::string& text, Doc& out, const std::string& name = std::string())
{
    return detail::parseWith([](ryml::csubstr n, ryml::csubstr src, ryml::Tree* t) { ryml::parse_json_in_arena(n, src, t); },
                             text, out, name);
}

// ---- reading ---------------------------------------------------------------------------------------

namespace detail {

// The UTF-16 surrogate a three-byte sequence at s[i] encodes (U+D800..U+DFFF), or 0 if there is none.
inline unsigned surrogateAt(const std::string& s, size_t i)
{
    if (i + 2 >= s.size()) return 0;
    const unsigned a = static_cast<unsigned char>(s[i]);
    const unsigned b = static_cast<unsigned char>(s[i + 1]);
    const unsigned c = static_cast<unsigned char>(s[i + 2]);
    if (a != 0xED || b < 0xA0 || b > 0xBF || (c & 0xC0) != 0x80) return 0;
    return 0xD000u | ((b & 0x3Fu) << 6) | (c & 0x3Fu);
}

// rapidyaml decodes the escape pair "😀" as two separate three-byte sequences rather than the
// one four-byte character it stands for - and JSON writers that escape everything non-ASCII produce
// exactly such pairs. Join each pair into its character; a lone half becomes U+FFFD. So does a NUL
// ("\u0000", "\0"), which would silently cut the text short for every c_str() consumer downstream.
inline void joinSurrogates(std::string& s)
{
    if (s.find('\xED') == std::string::npos && s.find('\0') == std::string::npos) return;
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();)
    {
        if (s[i] == '\0') { out += "\xEF\xBF\xBD"; ++i; continue; }
        const unsigned hi = surrogateAt(s, i);
        if (!hi) { out += s[i++]; continue; }
        const unsigned lo = hi < 0xDC00 ? surrogateAt(s, i + 3) : 0;
        if (lo >= 0xDC00)
        {
            const unsigned cp = 0x10000u + ((hi - 0xD800u) << 10) + (lo - 0xDC00u);
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
            i += 6;
        }
        else
        {
            out += "\xEF\xBF\xBD";
            i += 3;
        }
    }
    s.swap(out);
}

inline std::string str(ryml::csubstr v)
{
    std::string s(v.str ? v.str : "", v.len);
    joinSurrogates(s);
    return s;
}

}   // namespace detail

inline bool isMap(Node n) { return n.readable() && n.is_map(); }
inline bool isSeq(Node n) { return n.readable() && n.is_seq(); }

// A scalar holding a value: not a container, not a null, not an alias.
inline bool isScalar(Node n)
{
    return n.readable() && n.has_val() && !n.is_val_ref() && !n.val_is_null();
}

// The child of a map under `key`; not readable() when `map` is no map or has no such key.
inline Node child(Node map, const std::string& key)
{
    return isMap(map) ? map.find_child(view(key)) : Node();
}

// The key a map entry is filed under ("" for a list item).
inline std::string keyOf(Node n)
{
    return n.readable() && n.has_key() ? detail::str(n.key()) : std::string();
}

inline bool getText(Node n, std::string& out)
{
    if (!isScalar(n)) return false;
    out = detail::str(n.val());
    return true;
}

// A whole integer: optional sign, then decimal digits or 0x and hex digits. Anything else - a
// fraction, a unit, a value past the range of long long - does not fit.
inline bool getInteger(Node n, long long& out)
{
    std::string s;
    if (!getText(n, s)) return false;
    size_t i = 0;
    bool neg = false;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) neg = s[i++] == '-';
    unsigned base = 10;
    if (s.size() - i > 2 && s[i] == '0' && (s[i + 1] == 'x' || s[i + 1] == 'X')) { base = 16; i += 2; }
    if (i >= s.size()) return false;
    unsigned long long v = 0;
    for (; i < s.size(); ++i)
    {
        const char c = s[i];
        unsigned d;
        if (c >= '0' && c <= '9')                    d = static_cast<unsigned>(c - '0');
        else if (base == 16 && c >= 'a' && c <= 'f') d = static_cast<unsigned>(c - 'a' + 10);
        else if (base == 16 && c >= 'A' && c <= 'F') d = static_cast<unsigned>(c - 'A' + 10);
        else return false;
        if (v > (~0ull - d) / base) return false;
        v = v * base + d;
    }
    const unsigned long long maxPos = ~0ull >> 1;           // LLONG_MAX
    if (!neg) { if (v > maxPos) return false; out = static_cast<long long>(v); return true; }
    if (v > maxPos + 1) return false;
    out = v == maxPos + 1 ? -static_cast<long long>(maxPos) - 1 : -static_cast<long long>(v);
    return true;
}

namespace detail {

// JSON's number syntax, a leading '+' and a bare ".5" aside: sign, digits with at most one '.', an
// optional exponent - and nothing after it. c4::atod alone would take "1.5x" as 1.5.
inline bool isDecimalNumber(const std::string& s)
{
    size_t i = 0, digits = 0;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
    for (; i < s.size() && s[i] >= '0' && s[i] <= '9'; ++i) ++digits;
    if (i < s.size() && s[i] == '.')
        for (++i; i < s.size() && s[i] >= '0' && s[i] <= '9'; ++i) ++digits;
    if (!digits) return false;
    if (i < s.size() && (s[i] == 'e' || s[i] == 'E'))
    {
        ++i;
        if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
        size_t expDigits = 0;
        for (; i < s.size() && s[i] >= '0' && s[i] <= '9'; ++i) ++expDigits;
        if (!expDigits) return false;
    }
    return i == s.size();
}

}   // namespace detail

// A real number in the C locale's spelling whatever the UI locale is ("1.5", "-2e3").
inline bool getNumber(Node n, double& out)
{
    std::string s;
    if (!getText(n, s) || !detail::isDecimalNumber(s)) return false;
    return c4::atod(view(s), &out);
}

// true/false, and YAML 1.1's yes/no and on/off, in any case.
inline bool getBool(Node n, bool& out)
{
    std::string s;
    if (!getText(n, s)) return false;
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    if (s == "true" || s == "yes" || s == "on")  { out = true;  return true; }
    if (s == "false" || s == "no" || s == "off") { out = false; return true; }
    return false;
}

// Colours are spelled '#RRGGBB', as VS Code, Sublime and TextMate themes spell them; "RRGGBB" and any
// case read too. 0xRRGGBB out.
inline bool parseColor(const std::string& s, unsigned& rgb)
{
    const size_t i = (!s.empty() && s[0] == '#') ? 1 : 0;
    if (s.size() - i != 6) return false;
    unsigned v = 0;
    for (size_t k = i; k < s.size(); ++k)
    {
        const char c = s[k];
        unsigned d;
        if (c >= '0' && c <= '9') d = static_cast<unsigned>(c - '0');
        else if (c >= 'a' && c <= 'f') d = static_cast<unsigned>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') d = static_cast<unsigned>(c - 'A' + 10);
        else return false;
        v = v * 16 + d;
    }
    rgb = v;
    return true;
}
inline std::string colorText(unsigned rgb)
{
    static const char hex[] = "0123456789ABCDEF";
    std::string s = "#";
    for (int shift = 20; shift >= 0; shift -= 4) s += hex[(rgb >> shift) & 0xF];
    return s;
}
inline bool getColor(Node n, unsigned& rgb) { std::string s; return getText(n, s) && parseColor(s, rgb); }

inline std::string textOr(Node n, const std::string& fallback) { std::string s; return getText(n, s) ? s : fallback; }
inline long long   integerOr(Node n, long long fallback)        { long long v; return getInteger(n, v) ? v : fallback; }
inline bool        boolOr(Node n, bool fallback)                { bool v; return getBool(n, v) ? v : fallback; }

// What a node of a parseJson() document is. A string is a quoted scalar, so "1" and 1 stay apart; a
// plain scalar that is no JSON literal and no number is None.
enum class JsonType { None, Null, Bool, Number, String, Array, Object };

inline JsonType jsonType(Node n)
{
    if (!n.readable()) return JsonType::None;
    if (n.is_map()) return JsonType::Object;
    if (n.is_seq()) return JsonType::Array;
    if (!n.has_val() || n.is_val_ref()) return JsonType::None;
    if (n.is_val_quoted()) return JsonType::String;
    const ryml::csubstr v = n.val();
    if (v == "null") return JsonType::Null;
    if (v == "true" || v == "false") return JsonType::Bool;
    double d;
    return getNumber(n, d) ? JsonType::Number : JsonType::None;
}

// ---- writing ---------------------------------------------------------------------------------------

// rapidyaml nodes hold views, not copies: text from a temporary must be copied into the tree's own
// arena before a node can point at it.
inline ryml::csubstr keep(ryml::Tree& t, const std::string& s) { return t.copy_to_arena(view(s)); }

// Empty `t` and make its top node a map; returns that node.
inline MutNode resetToMap(ryml::Tree& t)
{
    t.clear();
    t.clear_arena();
    MutNode r = t.rootref();
    r |= ryml::MAP;
    return r;
}

// Append `key:` to a map. The new node becomes a value, a map or a list by what is set on it next.
inline MutNode addKey(MutNode map, const std::string& key)
{
    MutNode c = map.append_child();
    c.set_key(keep(*map.tree(), key));
    return c;
}
inline MutNode addMap(MutNode map, const std::string& key) { MutNode c = addKey(map, key); c |= ryml::MAP; return c; }
inline MutNode addSeq(MutNode map, const std::string& key) { MutNode c = addKey(map, key); c |= ryml::SEQ; return c; }
inline MutNode addItem(MutNode seq) { return seq.append_child(); }
inline MutNode addMapItem(MutNode seq) { MutNode c = seq.append_child(); c |= ryml::MAP; return c; }

// Write a map or a list on one line - `{id: 5, fg: "#0000FF"}` - as theme style rows are.
inline void setOneLine(MutNode container) { container |= ryml::FLOW_SL; }

namespace detail {

// Would `s`, written plain, read back as something else - a null, a bool, a number? Errs towards
// quoting: anything that merely starts like a number ("4 spaces", "1.0.0") counts.
inline bool readsAsOtherType(const std::string& s)
{
    if (s.empty()) return true;
    std::string l(s);
    for (char& c : l)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    static const char* const words[] = { "~", "null", "true", "false", "yes", "no", "on", "off", "y", "n",
                                         ".inf", "+.inf", "-.inf", ".nan" };
    for (const char* w : words)
        if (l == w) return true;
    size_t i = (l[0] == '+' || l[0] == '-') ? 1 : 0;
    if (i < l.size() && l[i] == '.') ++i;
    return i < l.size() && l[i] >= '0' && l[i] <= '9';
}

}   // namespace detail

inline void setText(MutNode n, const std::string& v)
{
    n.set_val(keep(*n.tree(), v));
    bool control = false;
    for (const char ch : v)
    {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (c < 0x20 || c == 0x7F) { control = true; break; }
    }
    if (control)                             n.set_val_style(ryml::VAL_DQUO);
    else if (detail::readsAsOtherType(v))    n.set_val_style(ryml::VAL_SQUO);
    else                                     n.set_val_style(ryml::NOTYPE);   // rapidyaml quotes only if plain would not read back
}
inline void setInteger(MutNode n, long long v)
{
    n.set_val(keep(*n.tree(), std::to_string(v)));
    n.set_val_style(ryml::VAL_PLAIN);
}
inline void setBool(MutNode n, bool v)
{
    n.set_val(ryml::to_csubstr(v ? "true" : "false"));
    n.set_val_style(ryml::VAL_PLAIN);
}

// The YAML for `t`, block style except where setOneLine() asked otherwise. False (and `out` untouched)
// if the tree cannot be written - a caller must then keep the file it has rather than write nothing.
inline bool emit(const ryml::Tree& t, std::string& out)
{
    if (t.size() == 0) { out.clear(); return true; }
    try
    {
        out = ryml::emitrs_yaml<std::string>(t, ryml::EmitOptions().force_flow_spc(true));
        return true;
    }
    catch (...)
    {
        return false;
    }
}

// The YAML for one node and everything under it, as it would stand at the top of a file ("key: value").
// "" if it cannot be written. Used to compare two documents entry by entry.
inline std::string emitNode(Node n)
{
    if (!n.readable()) return std::string();
    try
    {
        return ryml::emitrs_yaml<std::string>(n, ryml::EmitOptions().force_flow_spc(true));
    }
    catch (...)
    {
        return std::string();
    }
}

namespace detail {

// "key: value" as rapidyaml writes it, without the line break - so text spliced into a file by hand
// is quoted by exactly the rules setText() applies to a whole tree.
inline std::string emittedLine(const std::string& key, const std::string& value)
{
    ryml::Tree t;
    MutNode r = resetToMap(t);
    setText(addKey(r, key), value);
    std::string out;
    if (!emit(t, out)) return std::string();
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
    return out;
}

}   // namespace detail

// A UTF-8 byte-order mark at the start of `s` (as some Windows editors save), dropped. rapidyaml skips
// one by itself, but the line-based helpers here and SettingsFile's line editor read the raw text, where
// it would sit in front of the first key or comment.
inline void stripBom(std::string& s)
{
    if (s.compare(0, 3, "\xEF\xBB\xBF") == 0) s.erase(0, 3);
}

// A file's opening run of comment and blank lines - its header (licence, credits, how to edit it),
// which a store that writes the file out whole puts back at the top. A byte-order mark is not part of it.
inline std::string leadingComments(const std::string& text)
{
    std::string t = text;
    stripBom(t);
    size_t ls = 0;
    while (ls < t.size())
    {
        size_t le = t.find('\n', ls);
        const size_t next = le == std::string::npos ? t.size() : le + 1;
        if (le == std::string::npos) le = t.size();
        bool blank = true;
        for (size_t i = ls; i < le && blank; ++i) blank = t[i] == ' ' || t[i] == '\t' || t[i] == '\r';
        if (!blank && t[ls] != '#') break;
        ls = next;
    }
    std::string head = t.substr(0, ls);
    if (!head.empty() && head.back() != '\n') head += '\n';
    return head;
}

// How text `v` is spelled as a value after "key: ", on one line, quoted where it has to be.
inline std::string scalarYaml(const std::string& v)
{
    const std::string line = detail::emittedLine("k", v);       // "k: <value>"
    return line.size() > 3 ? line.substr(3) : std::string("''");
}
// How text `k` is spelled as a map key.
inline std::string keyYaml(const std::string& k)
{
    const std::string line = detail::emittedLine(k, "x");       // "<key>: x"
    return line.size() > 3 ? line.substr(0, line.size() - 3) : std::string("''");
}

}   // namespace wxnyaml
