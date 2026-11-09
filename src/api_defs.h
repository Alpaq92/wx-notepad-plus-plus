#pragma once
// SPDX-License-Identifier: Apache-2.0
//
// api_defs - api/<Language>.yaml: what completion and call tips know about one language's functions.
//
// Notepad++ keeps this in autoCompletion/<lang>.xml; Sublime Text in .sublime-completions, VS Code in a
// language server. Here it is one YAML file per language, named as the Language menu names it, read
// from wxNote's own api folder and then from the user's, where a function the user describes replaces
// wxNote's description of it:
//
//   ignoreCase: true                    # names match without regard to case (completion and call tips)
//   call: {open: '(', close: ')', separator: ',', end: ';'}   # how a call is written; `end` closes the tip
//   wordChars: '$'                      # what a name holds besides letters, digits and _
//   words: [echo, include, Loop Until]  # completion words that are not functions (or "echo include")
//   functions:
//     strlen: {returns: int, params: [string $string], doc: Returns the length of the string.}
//     substr:                           # one overload as a map, several as a list of them
//       - {returns: string, params: [string $string, int $offset]}
//       - {returns: string, params: [string $string, int $offset, '?int $length = null']}
//
// Nothing here throws, and a bad entry costs only itself: a value of the wrong shape is reported in
// `warnings` and skipped. Standalone - std and yaml_io.h, no wx - so tests/api_defs_test.cpp covers it whole.

#include "yaml_io.h"

#include <map>
#include <set>
#include <string>
#include <vector>

struct WxnApiOverload
{
    std::string returns;                 // "" for none
    std::vector<std::string> params;     // each as written, type and default included
    std::string doc;                     // "" for none; may hold line breaks
};

struct WxnApi
{
    // Each setting with whether the file gave it, so a user's file changes only what it says.
    bool ignoreCase = false;              bool hasIgnoreCase = false;
    char open = '(', close = ')', separator = ',', end = 0;   // end 0: none
    bool hasCall = false;
    std::string wordChars;                bool hasWordChars = false;
    std::vector<std::string> words;
    std::map<std::string, std::vector<WxnApiOverload>> functions;   // by name as written
    std::vector<std::string> warnings;

    bool empty() const { return words.empty() && functions.empty(); }

    static std::string lower(std::string s) { for (char& c : s) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a'); return s; }
    bool same(const std::string& a, const std::string& b) const { return ignoreCase ? lower(a) == lower(b) : a == b; }
    bool startsWith(const std::string& w, const std::string& prefix) const
    {
        return w.size() > prefix.size() && same(w.substr(0, prefix.size()), prefix);
    }

    // The overloads of the function called `name`, or nullptr.
    const std::vector<WxnApiOverload>* find(const std::string& name) const
    {
        const auto it = functions.find(name);
        if (it != functions.end()) return &it->second;
        if (ignoreCase)
            for (const auto& kv : functions) if (same(kv.first, name)) return &kv.second;
        return nullptr;
    }
    // Every function name and word that `prefix` begins (and is longer than), into `out`.
    template <class Set>
    void collect(const std::string& prefix, Set& out) const
    {
        for (const auto& kv : functions) if (startsWith(kv.first, prefix)) out.emplace(kv.first);
        for (const std::string& w : words) if (startsWith(w, prefix)) out.emplace(w);
    }
    // `over` laid on this: the settings it gives, its words added, its functions replacing these.
    void merge(const WxnApi& over)
    {
        if (over.hasIgnoreCase) { ignoreCase = over.ignoreCase; hasIgnoreCase = true; }
        if (over.hasCall) { open = over.open; close = over.close; separator = over.separator; end = over.end; hasCall = true; }
        if (over.hasWordChars) { wordChars = over.wordChars; hasWordChars = true; }
        std::set<std::string> have(words.begin(), words.end());
        for (const std::string& w : over.words) if (have.insert(w).second) words.push_back(w);
        for (const auto& kv : over.functions) functions[kv.first] = kv.second;
        warnings.insert(warnings.end(), over.warnings.begin(), over.warnings.end());
    }
};

// One overload as the call tip shows it: "int strlen(string $string)", then the description on the lines
// below. The first line is the signature; ctHighlight boxes a parameter in it by `api`'s call characters.
inline std::string wxnApiSignature(const std::string& name, const WxnApiOverload& o, const WxnApi& api)
{
    std::string s = o.returns.empty() ? std::string() : o.returns + " ";
    s += name;
    s += api.open;
    for (size_t i = 0; i < o.params.size(); ++i) { if (i) { s += api.separator; s += ' '; } s += o.params[i]; }
    s += api.close;
    if (!o.doc.empty()) s += "\n" + o.doc;
    return s;
}

// The file a language's API is kept in: its name, with what a file name cannot hold made '_'.
inline std::string wxnApiFileName(const std::string& language)
{
    std::string f = language;
    for (char& c : f) if (std::string("\\/:*?\"<>|").find(c) != std::string::npos || (unsigned char)c < 0x20) c = '_';
    return f + ".yaml";
}

namespace wxnapi_detail {

inline bool text(wxnyaml::Node v, std::string& s)
{
    if (wxnyaml::getText(v, s)) return true;
    long long i; double d; bool b;
    if (!v.readable() || v.is_container() || !v.has_val() || v.is_val_ref()) return false;
    if (wxnyaml::getInteger(v, i) || wxnyaml::getNumber(v, d) || wxnyaml::getBool(v, b)) { s = wxnyaml::detail::str(v.val()); return true; }
    return false;   // a null: nothing
}

// One call character: a single character, or "" for none where `optional`.
inline bool callChar(wxnyaml::Node v, char& c, bool optional)
{
    std::string s;
    if (!text(v, s)) return false;
    if (s.empty() && optional) { c = 0; return true; }
    if (s.size() != 1 || (unsigned char)s[0] <= ' ' || (unsigned char)s[0] >= 0x7F) return false;
    c = s[0];
    return true;
}

inline bool overload(wxnyaml::Node n, WxnApiOverload& o)
{
    if (!wxnyaml::isMap(n)) return false;
    for (wxnyaml::Node e : n.children())
    {
        const std::string k = wxnyaml::keyOf(e);
        if (k == "returns" || k == "doc")
        {
            if (e.is_container()) return false;
            text(e, k == "doc" ? o.doc : o.returns);   // a null leaves it empty
        }
        else if (k == "params")
        {
            std::string s;
            if (wxnyaml::isSeq(e)) { for (wxnyaml::Node p : e.children()) { if (!text(p, s)) return false; o.params.push_back(s); } }
            else if (text(e, s)) o.params.push_back(s);
            else if (e.is_container()) return false;
        }
        else return false;
    }
    return true;
}

}   // namespace wxnapi_detail

// Read an api/<Language>.yaml. False (with *err set) only for a file that does not parse or is not a map.
inline bool wxnParseApi(const std::string& source, WxnApi& out, std::string* err, const std::string& name = "api file")
{
    using namespace wxnapi_detail;
    out = WxnApi();
    std::string t = source;
    wxnyaml::stripBom(t);
    wxnyaml::Doc doc;
    if (!wxnyaml::parse(t, doc, name)) { if (err) *err = doc.error; return false; }
    const wxnyaml::Node root = doc.root();
    if (!root.readable()) return true;
    if (!wxnyaml::isMap(root)) { if (err) *err = "the top level is not a map"; return false; }
    std::vector<std::string>& warn = out.warnings;
    for (wxnyaml::Node e : root.children())
    {
        const std::string key = wxnyaml::keyOf(e);
        if (key == "ignoreCase")
        {
            if (wxnyaml::getBool(e, out.ignoreCase)) out.hasIgnoreCase = true;
            else warn.push_back("ignoreCase is not true or false");
        }
        else if (key == "wordChars")
        {
            if (text(e, out.wordChars)) out.hasWordChars = true;
            else warn.push_back("wordChars is not text");
        }
        else if (key == "call")
        {
            if (!wxnyaml::isMap(e)) { warn.push_back("call is not a map"); continue; }
            WxnApi c;   // all four from the defaults, then what is given
            bool ok = true;
            for (wxnyaml::Node f : e.children())
            {
                const std::string k = wxnyaml::keyOf(f);
                char* slot = k == "open" ? &c.open : k == "close" ? &c.close : k == "separator" ? &c.separator : k == "end" ? &c.end : nullptr;
                if (!slot) { warn.push_back("call: unknown entry \"" + k + "\""); ok = false; }
                else if (!callChar(f, *slot, k == "end")) { warn.push_back("call: " + k + " is not one character"); ok = false; }
            }
            if (!ok) continue;
            out.open = c.open; out.close = c.close; out.separator = c.separator; out.end = c.end; out.hasCall = true;
        }
        else if (key == "words")
        {
            // A list's entries are taken whole - Visual Basic completes "Loop Until" - and one line of
            // text is split on white space.
            std::string s;
            std::set<std::string> seen;
            auto put = [&](std::string w) {
                for (char& ch : w) if (ch == '\t' || ch == '\r' || ch == '\n') ch = ' ';
                const size_t a = w.find_first_not_of(' '), b = w.find_last_not_of(' ');
                if (a != std::string::npos && seen.insert(w.substr(a, b - a + 1)).second) out.words.push_back(w.substr(a, b - a + 1));
            };
            if (wxnyaml::isSeq(e)) { for (wxnyaml::Node w : e.children()) if (text(w, s)) put(s); }
            else if (text(e, s))
            {
                std::string cur;
                for (char ch : s + " ")
                    if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') { if (!cur.empty()) put(cur); cur.clear(); }
                    else cur += ch;
            }
            else if (e.is_container()) warn.push_back("words is not a list");
        }
        else if (key == "functions")
        {
            if (!wxnyaml::isMap(e)) { if (e.is_container()) warn.push_back("functions is not a map"); continue; }
            for (wxnyaml::Node f : e.children())
            {
                const std::string fn = wxnyaml::keyOf(f);
                if (fn.empty()) continue;
                std::vector<WxnApiOverload> overloads;
                bool ok = true;
                if (wxnyaml::isSeq(f))
                    for (wxnyaml::Node o : f.children()) { WxnApiOverload ov; if (overload(o, ov)) overloads.push_back(ov); else ok = false; }
                else if (wxnyaml::isMap(f)) { WxnApiOverload ov; if (overload(f, ov)) overloads.push_back(ov); else ok = false; }
                else if (f.is_container() || (f.has_val() && !f.val_is_null())) ok = false;
                else overloads.push_back(WxnApiOverload());   // `name:` alone - a function, nothing known of it
                if (!ok) warn.push_back("function " + fn + ": an overload is not {returns, params, doc}; it is left out");
                if (!overloads.empty()) out.functions[fn] = overloads;
            }
        }
        else warn.push_back("unknown entry \"" + key + "\"");
    }
    return true;
}

// The YAML for `api`, as wxnParseApi reads it back. `header` (comment lines, may be "") goes first.
inline std::string wxnEmitApi(const WxnApi& api, const std::string& header = std::string())
{
    ryml::Tree tree{ wxnyaml::detail::callbacks() };
    wxnyaml::MutNode root = wxnyaml::resetToMap(tree);
    if (api.hasIgnoreCase) wxnyaml::setBool(wxnyaml::addKey(root, "ignoreCase"), api.ignoreCase);
    if (api.hasCall)
    {
        wxnyaml::MutNode c = wxnyaml::addMap(root, "call");
        wxnyaml::setOneLine(c);
        wxnyaml::setText(wxnyaml::addKey(c, "open"), std::string(1, api.open));
        wxnyaml::setText(wxnyaml::addKey(c, "close"), std::string(1, api.close));
        wxnyaml::setText(wxnyaml::addKey(c, "separator"), std::string(1, api.separator));
        if (api.end) wxnyaml::setText(wxnyaml::addKey(c, "end"), std::string(1, api.end));
    }
    if (api.hasWordChars) wxnyaml::setText(wxnyaml::addKey(root, "wordChars"), api.wordChars);
    if (!api.words.empty())
    {
        wxnyaml::MutNode w = wxnyaml::addSeq(root, "words");
        wxnyaml::setOneLine(w);
        for (const std::string& s : api.words) wxnyaml::setText(wxnyaml::addItem(w), s);
    }
    if (!api.functions.empty())
    {
        wxnyaml::MutNode fs = wxnyaml::addMap(root, "functions");
        auto put = [](wxnyaml::MutNode m, const WxnApiOverload& o) {
            wxnyaml::setOneLine(m);
            if (!o.returns.empty()) wxnyaml::setText(wxnyaml::addKey(m, "returns"), o.returns);
            wxnyaml::MutNode p = wxnyaml::addSeq(m, "params");
            for (const std::string& s : o.params) wxnyaml::setText(wxnyaml::addItem(p), s);
            if (!o.doc.empty()) wxnyaml::setText(wxnyaml::addKey(m, "doc"), o.doc);
        };
        for (const auto& kv : api.functions)
        {
            if (kv.second.size() == 1) { wxnyaml::MutNode m = wxnyaml::addMap(fs, kv.first); put(m, kv.second[0]); continue; }
            wxnyaml::MutNode list = wxnyaml::addSeq(fs, kv.first);
            for (const WxnApiOverload& o : kv.second) put(wxnyaml::addMapItem(list), o);
        }
    }
    std::string out;
    if (!wxnyaml::emit(tree, out)) return std::string();
    return header + out;
}
