// SPDX-License-Identifier: Apache-2.0
//
// wxNote - settings_schema.h: every setting settings.yaml can hold, and typed access to them.
// Copyright 2026 The wxNote Authors.
//
// One table - ID, type, default, accepted range or names - read by the loader, by the writer that keeps
// settings.yaml a list of differences, and by the tests, so a setting cannot be added in one place and
// forgotten in another. docs/SETTINGS_DESIGN.md lists the same settings for people.
//
// Reading never fails: a missing value, one of the wrong type or one out of range reads as the default.
// Writing a value equal to the default removes it from the file.

#pragma once

#include "settings_store.h"

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace wxnsettings {

enum class Kind { Bool, Int, Text, Choice, Color, Map };

struct Def
{
    const char* id;
    Kind        kind;
    const char* def;                // the default, spelled as in the file ("" for none)
    long long   lo = 0, hi = 0;     // Int: the accepted range
    const char* choices = nullptr;  // Choice: the names, '|'-separated, in the order the code numbers them
    bool        perLanguage = false;
};

#ifdef _WIN32
#define WXN_DEFAULT_BUTTON_STYLE "native"
#else
#define WXN_DEFAULT_BUTTON_STYLE "flat"
#endif

inline const std::vector<Def>& schema()
{
    static const std::vector<Def> s = {
        { "ui.language",                      Kind::Text,   "system" },
        { "ui.themeMode",                     Kind::Choice, "system", 0, 0, "system|dark|light" },
        { "ui.colorTheme",                    Kind::Text,   "" },
        { "ui.toolbar.visible",               Kind::Bool,   "true" },
        { "ui.toolbar.iconStyle",             Kind::Choice, "solar", 0, 0, "line|solar|iconpark|streamline" },
        { "ui.toolbar.iconSize",              Kind::Int,    "16", 12, 64 },
        { "ui.statusBar.visible",             Kind::Bool,   "true" },
        { "ui.statusBar.zoomField",           Kind::Bool,   "false" },
        { "ui.tabs.closeButton",              Kind::Bool,   "true" },
        { "ui.fullScreen.hideToolbar",        Kind::Bool,   "false" },
        { "window.integratedTitleBar",        Kind::Bool,   "false" },
        { "window.buttonStyle",               Kind::Choice, WXN_DEFAULT_BUTTON_STYLE, 0, 0, "native|flat|minimal" },
        { "window.ignorePlatformDecorations", Kind::Bool,   "false" },
        { "window.reuseInstance",             Kind::Bool,   "false" },
        { "editor.fontFamily",                Kind::Text,   "Cascadia Mono" },
        { "editor.tabSize",                   Kind::Int,    "4", 1, 16, nullptr, true },
        { "editor.useTabs",                   Kind::Bool,   "true", 0, 0, nullptr, true },
        { "editor.lineNumbers",               Kind::Bool,   "true" },
        { "editor.wordWrap",                  Kind::Bool,   "false" },
        { "editor.wrapSymbol",                Kind::Bool,   "false" },
        { "editor.showWhitespace",            Kind::Bool,   "false" },
        { "editor.indentGuides",              Kind::Bool,   "true" },
        { "editor.highlightCurrentLine",      Kind::Bool,   "true" },
        { "editor.autoIndent",                Kind::Bool,   "true" },
        { "editor.multiCursor",               Kind::Bool,   "true" },
        { "editor.scrollBeyondLastLine",      Kind::Bool,   "false" },
        { "editor.caretWidth",                Kind::Int,    "1", 1, 3 },
        { "editor.caretBlinkMs",              Kind::Int,    "500", 0, 2000 },
        { "editor.edgeColumn",                Kind::Int,    "0", 0, 300 },
        { "editor.directWrite",               Kind::Bool,   "true" },
        { "editor.customGutterColor",         Kind::Bool,   "false" },
        { "editor.gutterColor",               Kind::Color,  "" },
        { "editor.longLineThreshold",         Kind::Int,    "50000", 0, 1000000 },
        { "editor.autoComplete.enabled",      Kind::Bool,   "true" },
        { "editor.autoComplete.minChars",     Kind::Int,    "3", 1, 10 },
        { "editor.autoClosePairs",            Kind::Bool,   "false" },
        { "files.associations",               Kind::Map,    "" },
        { "files.largeFileThresholdMiB",      Kind::Int,    "16", 0, 4096 },
        { "files.maxRecentFiles",             Kind::Int,    "10", 1, 50 },
        { "files.confirmCloseUnsaved",        Kind::Bool,   "false" },
        { "files.defaultDirectory",           Kind::Choice, "follow", 0, 0, "follow|remember|fixed" },
        { "files.defaultDirectoryPath",       Kind::Text,   "" },
        { "files.newDocument.eol",            Kind::Choice, "crlf", 0, 0, "crlf|cr|lf" },   // = SC_EOL_CRLF/CR/LF
        { "files.newDocument.language",       Kind::Text,   "" },
        { "files.newDocument.encoding",       Kind::Choice, "utf-8", 0, 0, "utf-8|utf-8-bom|utf-16le|utf-16be|ansi" },
        { "search.webEngine",                 Kind::Choice, "duckduckgo", 0, 0, "duckduckgo|google|bing|yahoo|brave" },
        { "spelling.backend",                 Kind::Choice, "native+hunspell", 0, 0, "native|native+hunspell|hunspell" },
        { "spelling.dictionary",              Kind::Text,   "en_US" },
        { "spelling.commentsOnly",            Kind::Bool,   "true" },
        { "print.header",                     Kind::Text,   "" },
        { "print.footer",                     Kind::Text,   "" },
    };
    return s;
}

inline const Def* findDef(const std::string& id)
{
    static const std::unordered_map<std::string, const Def*> index = [] {
        std::unordered_map<std::string, const Def*> m;
        for (const Def& d : schema()) m.emplace(d.id, &d);
        return m;
    }();
    auto it = index.find(id);
    return it == index.end() ? nullptr : it->second;
}

// The names of a Choice setting, in index order.
inline std::vector<std::string> choiceNames(const Def& d)
{
    std::vector<std::string> out;
    if (!d.choices) return out;
    std::string cur;
    for (const char* p = d.choices;; ++p)
    {
        if (*p == '|' || !*p) { out.push_back(cur); cur.clear(); if (!*p) break; }
        else cur += *p;
    }
    return out;
}

namespace detail {

inline std::string lower(std::string s)
{
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

}   // namespace detail

// Typed access to a SettingsFile, by setting ID.
class Settings
{
public:
    explicit Settings(SettingsFile& file) : m_file(file) {}

    bool getBool(const char* id) const { return readBool(m_file.get(id), id); }
    long long getInt(const char* id) const { return readInt(m_file.get(id), id); }
    std::string getText(const char* id) const
    {
        const Def* d = findDef(id);
        std::string s;
        return wxnyaml::getText(m_file.get(id), s) ? s : (d ? d->def : "");
    }
    int getChoice(const char* id) const
    {
        const Def* d = findDef(id);
        if (!d) return 0;
        const std::vector<std::string> names = choiceNames(*d);
        std::string s;
        if (wxnyaml::getText(m_file.get(id), s))
        {
            s = detail::lower(s);
            for (size_t i = 0; i < names.size(); ++i)
                if (names[i] == s) return static_cast<int>(i);
        }
        for (size_t i = 0; i < names.size(); ++i)
            if (names[i] == d->def) return static_cast<int>(i);
        return 0;
    }
    std::string getChoiceName(const char* id) const
    {
        const Def* d = findDef(id);
        if (!d) return std::string();
        const std::vector<std::string> names = choiceNames(*d);
        const int i = getChoice(id);
        return i >= 0 && i < static_cast<int>(names.size()) ? names[i] : std::string();
    }
    // False when the setting is absent or not a colour: the caller has its own fallback (the gutter's
    // follows the theme).
    bool getColor(const char* id, unsigned& rgb) const
    {
        std::string s;
        return wxnyaml::getText(m_file.get(id), s) && wxnyaml::parseColor(s, rgb);
    }
    std::vector<std::pair<std::string, std::string>> getMap(const char* id) const
    {
        std::vector<std::pair<std::string, std::string>> out;
        const Node m = m_file.get(id);
        if (wxnyaml::isMap(m))
            for (Node c : m.children())
            {
                std::string v;
                if (wxnyaml::getText(c, v)) out.emplace_back(wxnyaml::keyOf(c), v);
            }
        return out;
    }
    // A per-language setting's own value for `lang` (a Language-menu name), from `languages: <lang>:` -
    // false when that language has no valid value of its own (or the setting is not per-language), so
    // the caller uses the general one.
    bool languageInt(const char* id, const std::string& lang, long long& out) const
    {
        const Def* d = findDef(id);
        long long v;
        if (!d || !d->perLanguage || lang.empty() || !wxnyaml::getInteger(m_file.getForLanguage(lang, id), v) || v < d->lo || v > d->hi)
            return false;
        out = v;
        return true;
    }
    bool languageBool(const char* id, const std::string& lang, bool& out) const
    {
        const Def* d = findDef(id);
        return d && d->perLanguage && !lang.empty() && wxnyaml::getBool(m_file.getForLanguage(lang, id), out);
    }

    // Writes. A value equal to the default is removed from the file instead. False when the file
    // refused the edit (it does not parse) or the ID is unknown.
    bool setBool(const char* id, bool v)
    {
        const Def* d = findDef(id);
        if (!d || d->kind != Kind::Bool) return false;
        if ((std::string(d->def) == "true") == v) return m_file.remove(id);
        return m_file.set(id, v ? "true" : "false", v ? "true" : "false");
    }
    bool setInt(const char* id, long long v)
    {
        const Def* d = findDef(id);
        if (!d || d->kind != Kind::Int) return false;
        const std::string s = std::to_string(v);
        if (s == d->def) return m_file.remove(id);
        return m_file.set(id, s, s);
    }
    bool setText(const char* id, const std::string& v)
    {
        const Def* d = findDef(id);
        if (!d || d->kind != Kind::Text) return false;
        if (v == d->def) return m_file.remove(id);
        return m_file.set(id, wxnyaml::scalarYaml(v), v);
    }
    bool setChoice(const char* id, int index)
    {
        const Def* d = findDef(id);
        if (!d || d->kind != Kind::Choice) return false;
        const std::vector<std::string> names = choiceNames(*d);
        if (index < 0 || index >= static_cast<int>(names.size())) return false;
        const std::string& name = names[index];
        if (name == d->def) return m_file.remove(id);
        return m_file.set(id, wxnyaml::scalarYaml(name), name);
    }
    bool setColor(const char* id, unsigned rgb)
    {
        const Def* d = findDef(id);
        if (!d || d->kind != Kind::Color) return false;
        const std::string s = wxnyaml::colorText(rgb);
        return m_file.set(id, wxnyaml::scalarYaml(s), s);
    }
    bool setMap(const char* id, const std::vector<std::pair<std::string, std::string>>& entries)
    {
        const Def* d = findDef(id);
        if (!d || d->kind != Kind::Map) return false;
        return m_file.setMap(id, entries);
    }

private:
    SettingsFile& m_file;

    static bool readBool(Node n, const char* id)
    {
        bool v;
        if (wxnyaml::getBool(n, v)) return v;
        const Def* d = findDef(id);
        return d && std::string(d->def) == "true";
    }
    static long long readInt(Node n, const char* id)
    {
        const Def* d = findDef(id);
        long long v;
        if (d && wxnyaml::getInteger(n, v) && v >= d->lo && v <= d->hi) return v;
        return d ? std::stoll(d->def) : 0;
    }
};

}   // namespace wxnsettings
