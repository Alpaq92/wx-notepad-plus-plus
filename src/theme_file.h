// SPDX-License-Identifier: Apache-2.0
//
// wxNote - theme_file.h: a colour theme as its file holds it (themes/<name>.yaml).
// Copyright 2026 The wxNote Authors.
//
//   # Monokai - ...                        <- the comment block at the top is kept by every write
//   global:                                <- widget colours, by name
//     Default Style: {fg: '#F8F8F2', bg: '#272822', font: Cascadia Mono, size: 10}
//   lexers:                                <- a style table per lexer
//     cpp:
//       description: C++
//       extensions: h hpp                  <- optional: Notepad++'s "user extensions" for the language
//       styles:
//         - {id: 5, name: INSTRUCTION WORD, fg: '#F92672', fontStyle: bold}
//
// An absent field inherits. fontStyle is any of bold / italic / underline ("bold italic"), or normal.
// docs/SETTINGS_DESIGN.md has the reasoning; src/main.cpp turns this into the editor's styles, and the
// GPL npp-compat package translates Notepad++'s XML themes into it. No wx, so both - and the tests - can.

#pragma once

#include "yaml_io.h"

#include <string>
#include <vector>

namespace wxntheme {

struct Style
{
    int         id = -1;            // Scintilla style number (lexer styles; unused for global ones)
    std::string name;               // shown in the Style Configurator
    int         fg = -1, bg = -1;   // 0xRRGGBB; -1 = inherit
    int         fontStyle = 0;      // 1 bold | 2 italic | 4 underline
    std::string font;               // "" = inherit
    int         size = 0;           // points; 0 = inherit
    int         weight = 0;         // 100..900; 0 = inherit
};

struct Lexer
{
    std::string        name;          // the style table's key: "cpp", "javascript.js", "genericLangDef"
    std::string        description;
    std::string        extensions;    // space-separated
    std::vector<Style> styles;
};

struct Theme
{
    std::string        header;        // the comment block at the top, "# " included, kept as it is
    std::vector<Style> globals;       // by name, in file order
    std::vector<Lexer> lexers;        // in file order

    Style* global(const std::string& n)
    {
        for (Style& s : globals) if (s.name == n) return &s;
        return nullptr;
    }
    Lexer* lexer(const std::string& n)
    {
        for (Lexer& l : lexers) if (l.name == n) return &l;
        return nullptr;
    }
};

namespace detail {

inline int fontStyleFrom(const std::string& text)
{
    int bits = 0;
    std::string word;
    for (size_t i = 0; i <= text.size(); ++i)
    {
        const char c = i < text.size() ? text[i] : ' ';
        if (c != ' ' && c != ',' && c != '\t') { word += (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; continue; }
        if (word == "bold") bits |= 1;
        else if (word == "italic") bits |= 2;
        else if (word == "underline") bits |= 4;
        word.clear();
    }
    return bits;
}

inline std::string fontStyleText(int bits)
{
    std::string s;
    if (bits & 1) s += "bold";
    if (bits & 2) s += s.empty() ? "italic" : " italic";
    if (bits & 4) s += s.empty() ? "underline" : " underline";
    return s;
}

inline void readStyle(wxnyaml::Node n, Style& s)
{
    unsigned rgb = 0;
    if (wxnyaml::getColor(wxnyaml::child(n, "fg"), rgb)) s.fg = static_cast<int>(rgb);
    if (wxnyaml::getColor(wxnyaml::child(n, "bg"), rgb)) s.bg = static_cast<int>(rgb);
    s.fontStyle = fontStyleFrom(wxnyaml::textOr(wxnyaml::child(n, "fontStyle"), std::string()));
    s.font = wxnyaml::textOr(wxnyaml::child(n, "font"), std::string());
    const long long size = wxnyaml::integerOr(wxnyaml::child(n, "size"), 0);
    s.size = size > 0 && size <= 400 ? static_cast<int>(size) : 0;
    const long long weight = wxnyaml::integerOr(wxnyaml::child(n, "weight"), 0);
    s.weight = weight >= 1 && weight <= 1000 ? static_cast<int>(weight) : 0;
}

inline void writeStyle(wxnyaml::MutNode m, const Style& s, bool withIdAndName)
{
    wxnyaml::setOneLine(m);
    if (withIdAndName)
    {
        wxnyaml::setInteger(wxnyaml::addKey(m, "id"), s.id);
        wxnyaml::setText(wxnyaml::addKey(m, "name"), s.name);
    }
    if (s.fg >= 0) wxnyaml::setText(wxnyaml::addKey(m, "fg"), wxnyaml::colorText(static_cast<unsigned>(s.fg)));
    if (s.bg >= 0) wxnyaml::setText(wxnyaml::addKey(m, "bg"), wxnyaml::colorText(static_cast<unsigned>(s.bg)));
    if (s.fontStyle) wxnyaml::setText(wxnyaml::addKey(m, "fontStyle"), fontStyleText(s.fontStyle));
    if (!s.font.empty()) wxnyaml::setText(wxnyaml::addKey(m, "font"), s.font);
    if (s.size > 0) wxnyaml::setInteger(wxnyaml::addKey(m, "size"), s.size);
    if (s.weight > 0) wxnyaml::setInteger(wxnyaml::addKey(m, "weight"), s.weight);
}

}   // namespace detail

// Read a theme. False (with the reason in *err) when the text does not parse or holds no theme.
inline bool parse(const std::string& text, Theme& out, std::string* err = nullptr)
{
    out = Theme();
    wxnyaml::Doc doc;
    if (!wxnyaml::parse(text, doc, "theme"))
    {
        if (err) *err = doc.error;
        return false;
    }
    const wxnyaml::Node root = doc.root();
    if (!wxnyaml::isMap(root))
    {
        if (err) *err = "not a theme: no global: or lexers: section";
        return false;
    }
    out.header = wxnyaml::leadingComments(text);
    const wxnyaml::Node globals = wxnyaml::child(root, "global");
    if (wxnyaml::isMap(globals))
        for (wxnyaml::Node g : globals.children())
        {
            Style s;
            s.name = wxnyaml::keyOf(g);
            if (s.name.empty()) continue;
            if (wxnyaml::isMap(g)) detail::readStyle(g, s);
            out.globals.push_back(std::move(s));
        }
    const wxnyaml::Node lexers = wxnyaml::child(root, "lexers");
    if (wxnyaml::isMap(lexers))
        for (wxnyaml::Node l : lexers.children())
        {
            Lexer lx;
            lx.name = wxnyaml::keyOf(l);
            if (lx.name.empty()) continue;
            lx.description = wxnyaml::textOr(wxnyaml::child(l, "description"), std::string());
            lx.extensions  = wxnyaml::textOr(wxnyaml::child(l, "extensions"), std::string());
            const wxnyaml::Node styles = wxnyaml::child(l, "styles");
            if (wxnyaml::isSeq(styles))
                for (wxnyaml::Node sn : styles.children())
                {
                    long long id = -1;
                    if (!wxnyaml::isMap(sn) || !wxnyaml::getInteger(wxnyaml::child(sn, "id"), id) || id < 0 || id > 255) continue;
                    Style s;
                    s.id = static_cast<int>(id);
                    s.name = wxnyaml::textOr(wxnyaml::child(sn, "name"), std::string());
                    detail::readStyle(sn, s);
                    lx.styles.push_back(std::move(s));
                }
            out.lexers.push_back(std::move(lx));
        }
    if (out.globals.empty() && out.lexers.empty())
    {
        if (err) *err = "not a theme: no global: or lexers: section";
        return false;
    }
    return true;
}

// The theme as its file: the header, then global: and lexers:. Empty on failure.
inline std::string emit(const Theme& t)
{
    ryml::Tree tree;
    wxnyaml::MutNode root = wxnyaml::resetToMap(tree);
    wxnyaml::MutNode globals = wxnyaml::addMap(root, "global");
    for (const Style& s : t.globals) detail::writeStyle(wxnyaml::addMap(globals, s.name), s, false);
    wxnyaml::MutNode lexers = wxnyaml::addMap(root, "lexers");
    for (const Lexer& l : t.lexers)
    {
        wxnyaml::MutNode lm = wxnyaml::addMap(lexers, l.name);
        if (!l.description.empty()) wxnyaml::setText(wxnyaml::addKey(lm, "description"), l.description);
        if (!l.extensions.empty()) wxnyaml::setText(wxnyaml::addKey(lm, "extensions"), l.extensions);
        wxnyaml::MutNode styles = wxnyaml::addSeq(lm, "styles");
        for (const Style& s : l.styles) detail::writeStyle(wxnyaml::addMapItem(styles), s, true);
    }
    std::string out;
    if (!wxnyaml::emit(tree, out)) return std::string();
    return t.header + out;
}

}   // namespace wxntheme
