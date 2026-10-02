#pragma once
// SPDX-License-Identifier: Apache-2.0
//
// language_defs - languages.yaml: the user's changes to what wxNote knows about each language - which
// files open as it, how it is commented, and its keyword lists.
//
// Notepad++ keeps these in langs.xml, a copy of its built-in data that the user edits in place. Here, as
// in VS Code, Sublime Text, Pulsar and TextMate, the built-in data stays in the program and the file holds
// only what the user changed, so a later wxNote's new keywords or extensions still arrive:
//
//   languages:
//     C++:                                     # the Language menu's name, as in settings.yaml
//       extensions: {add: [ipp, tpp]}          # a list replaces wxNote's own; {add, remove} edits it
//       filenames: [conanfile.txt]             # whole file names, any case; the same two forms
//       firstLine: '^//.*-\*-\s*c\+\+'         # a regex: a first line it matches opens as this language
//       comments: {line: '//', block: ['/*', '*/']}   # '' or [] takes a form away
//       keywords:
//         types: {add: [size_t, ssize_t]}      # each list by its name (keyword_sets.h), same two forms
//         userKeywords1: [Q_OBJECT, emit]      # user keyword groups: the theme's USER KEYWORDS 1-8
//     Python:
//       keywords: {add: [match, case]}         # short for the language's "keywords" list (else its first)
//
// The names follow the other editors where they agree: extensions without the dot and filenames
// (Sublime Text, TextMate, Pulsar), a firstLine pattern (VS Code; firstLineMatch elsewhere), a comment
// as a line token and an open/close pair (VS Code, Pulsar). Edits as {add, remove} over the built-in
// list follow TextMate's deltas over a bundle: a removal stays removed when wxNote's own list grows.
//
// Nothing here throws, and a bad entry costs only itself: an unknown language or list name, or a value
// of the wrong shape, is reported in `warnings` and skipped. Standalone - std and yaml_io.h, no wx - so
// tests/language_defs_test.cpp covers it whole.

#include "comment_tokens.h"
#include "keyword_sets.h"
#include "keywords.h"
#include "lang_detect.h"
#include "yaml_io.h"

#include <algorithm>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <vector>

// A list as languages.yaml changes it: replaced whole, or edited.
struct WxnListEdit
{
    bool given   = false;                // the file says something about this list
    bool replace = false;                // `words` is the whole list
    std::vector<std::string> words;      // the list (replace), or what it adds
    std::vector<std::string> remove;     // what it takes away (edit form)

    // `base` with the edit applied: the replacement, or base less the removals plus the additions, in
    // order and each word once.
    std::vector<std::string> apply(const std::vector<std::string>& base) const
    {
        std::vector<std::string> out;
        std::set<std::string> seen;
        const std::set<std::string> gone(remove.begin(), remove.end());
        auto put = [&](const std::string& w) { if (!w.empty() && !gone.count(w) && seen.insert(w).second) out.push_back(w); };
        if (!replace) for (const std::string& w : base) put(w);
        for (const std::string& w : words) put(w);
        return out;
    }
};

struct WxnLangDef
{
    std::string name;                            // Language-menu name
    WxnListEdit extensions;                      // lower case, no dot
    WxnListEdit filenames;                       // lower case
    std::string firstLine;                       // a regex, "" for none
    bool hasLineComment = false, hasBlockComment = false;
    std::string lineComment, blockOpen, blockClose;
    std::map<std::string, WxnListEdit> keywords; // list or group name -> edit
};

struct WxnLangDefs
{
    std::vector<WxnLangDef> langs;               // in file order
    std::vector<std::string> warnings;           // what was skipped, and why

    const WxnLangDef* find(const std::string& language) const
    {
        for (const WxnLangDef& d : langs) if (d.name == language) return &d;
        return nullptr;
    }
};

// Make `language`'s list `list` add `words` in `defs`, as wxnLangDefsSetAdded (below) writes it into the file:
// the Style Configurator's User-defined keywords, before they are saved. A list the file replaces whole
// stays a replacement (the box edits only a user keyword group's); one left saying nothing goes.
inline void wxnLangDefsSetWords(WxnLangDefs& defs, const std::string& language, const std::string& list,
                                const std::vector<std::string>& words)
{
    WxnLangDef* def = nullptr;
    for (WxnLangDef& d : defs.langs) if (d.name == language) def = &d;
    if (!def)
    {
        if (words.empty()) return;
        defs.langs.emplace_back();
        def = &defs.langs.back();
        def->name = language;
    }
    WxnListEdit& edit = def->keywords[list];
    edit.replace = edit.given && edit.replace;
    edit.given = true;
    edit.words = words;
    if (edit.words.empty() && edit.remove.empty()) def->keywords.erase(list);
}

// A value as words are read from it: its text, where a plain null, Null or NULL is a word like any other
// - NULL is a C macro, null a JavaScript keyword - though YAML reads it as nothing. False for no text.
inline bool wxnDefText(wxnyaml::Node v, std::string& s)
{
    if (wxnyaml::getText(v, s)) return true;
    if (!v.readable() || v.is_container() || !v.has_val() || v.is_val_ref()) return false;
    s = wxnyaml::detail::str(v.val());
    return s == "null" || s == "Null" || s == "NULL";
}

// YAML's nothing - a key with no value, or ~ - which says nothing about what it names.
inline bool wxnDefIsNothing(wxnyaml::Node v)
{
    std::string s;
    return v.readable() && !v.is_container() && v.has_val() && !v.is_val_ref() && v.val_is_null() && !wxnDefText(v, s);
}

// Words from a scalar ("a b c") or a list of them, split on white space.
inline std::vector<std::string> wxnDefWords(wxnyaml::Node n)
{
    std::vector<std::string> out;
    auto split = [&](const std::string& s) {
        std::string cur;
        for (char c : s + " ")
        {
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { if (!cur.empty()) out.push_back(cur); cur.clear(); }
            else cur += c;
        }
    };
    std::string s;
    if (wxnDefText(n, s)) split(s);
    else if (wxnyaml::isSeq(n))
        for (wxnyaml::Node c : n.children()) if (wxnDefText(c, s)) split(s);
    return out;
}

// A list's value: a list or a string replaces, {add, remove} edits. `normalize` turns each word into
// its stored form, "" for one to drop with a warning. False for a value of another shape.
template <class Normalize>
bool wxnReadListEdit(wxnyaml::Node n, WxnListEdit& out, Normalize&& normalize, std::vector<std::string>& bad)
{
    auto take = [&](wxnyaml::Node v, std::vector<std::string>& into) {
        for (const std::string& w : wxnDefWords(v))
        {
            const std::string k = normalize(w);
            if (k.empty()) bad.push_back(w);
            else into.push_back(k);
        }
    };
    if (wxnyaml::isMap(n))
    {
        for (wxnyaml::Node c : n.children())
        {
            const std::string key = wxnyaml::keyOf(c);
            if (key != "add" && key != "remove") return false;
        }
        take(wxnyaml::child(n, "add"), out.words);
        take(wxnyaml::child(n, "remove"), out.remove);
        out.given = true;
        out.replace = false;
        return true;
    }
    std::string text;
    if (wxnyaml::isSeq(n) || wxnDefText(n, text))
    {
        take(n, out.words);
        out.given = true;
        out.replace = true;
        return true;
    }
    return false;
}

// Is `n` an {add, remove} map (and nothing else)?
inline bool wxnIsListEditMap(wxnyaml::Node n)
{
    if (!wxnyaml::isMap(n) || !n.has_children()) return false;
    for (wxnyaml::Node c : n.children())
    {
        const std::string key = wxnyaml::keyOf(c);
        if (key != "add" && key != "remove") return false;
    }
    return true;
}

// The names `language` (on `lexer`) has keyword lists and user keyword groups under, in order.
inline std::vector<std::string> wxnKeywordListNames(const std::string& lexer, const std::string& language)
{
    std::vector<std::string> out;
    for (const WxnKeywordSetName* s : wxnKeywordSetsOf(lexer)) out.push_back(s->name);
    for (const WxnSubstyleGroup& g : wxnSubstyleGroupsOf(lexer, language)) out.push_back(g.name);
    return out;
}

// What `keywords: [...]` without a list name means: the list called "keywords", else the first.
inline std::string wxnMainKeywordList(const std::string& lexer)
{
    const std::vector<const WxnKeywordSetName*> sets = wxnKeywordSetsOf(lexer);
    for (const WxnKeywordSetName* s : sets) if (std::string(s->name) == "keywords") return s->name;
    return sets.empty() ? std::string() : std::string(sets.front()->name);
}

// Parse languages.yaml. False, with *err set, for text that does not parse or whose top level is not a
// map; an empty file (or one of comments only) is fine and changes nothing.
//   canonical(name)  the Language-menu name `name` stands for ("" if none) - matched without regard to case
//   lexerOf(name)    that language's lexer
template <class Canonical, class LexerOf>
bool wxnParseLangDefs(const std::string& text, WxnLangDefs& out, std::string* err, Canonical&& canonical, LexerOf&& lexerOf)
{
    out = WxnLangDefs();
    std::string t = text;
    wxnyaml::stripBom(t);
    wxnyaml::Doc doc;
    if (!wxnyaml::parse(t, doc, "languages.yaml")) { if (err) *err = doc.error; return false; }
    const wxnyaml::Node root = doc.root();
    if (!root.readable()) return true;
    if (!wxnyaml::isMap(root)) { if (err) *err = "the top level is not a map"; return false; }
    std::vector<std::string>& warn = out.warnings;
    for (wxnyaml::Node top : root.children())
        if (wxnyaml::keyOf(top) != "languages") warn.push_back("unknown entry \"" + wxnyaml::keyOf(top) + "\"");
    const wxnyaml::Node langs = wxnyaml::child(root, "languages");
    if (!langs.readable()) return true;
    if (!wxnyaml::isMap(langs))
    {
        if (wxnyaml::isScalar(langs) || wxnyaml::isSeq(langs)) warn.push_back("\"languages\" is not a map");
        return true;   // `languages:` with nothing under it, as the template has it, is fine
    }

    // Nothing - a key with no value - says nothing, wherever it stands (but in comments: see there). A key
    // given twice counts once, as the writers below read it: the first.
    std::set<std::string> named;
    for (wxnyaml::Node ln : langs.children())
    {
        const std::string written = wxnyaml::keyOf(ln);
        const std::string name = canonical(written);
        if (name.empty()) { warn.push_back("unknown language \"" + written + "\""); continue; }
        if (!named.insert(name).second) { warn.push_back(name + " is given twice; the second is not used"); continue; }
        if (wxnDefIsNothing(ln)) continue;
        if (!wxnyaml::isMap(ln)) { warn.push_back(name + ": not a map"); continue; }
        const std::string lexer = lexerOf(name);
        WxnLangDef def;
        def.name = name;
        std::set<std::string> seen;
        for (wxnyaml::Node e : ln.children())
        {
            const std::string key = wxnyaml::keyOf(e);
            std::vector<std::string> bad;
            if (!seen.insert(key).second) { warn.push_back(name + ": " + key + " is given twice; the second is not used"); continue; }
            if (wxnDefIsNothing(e)) continue;
            if (key == "extensions")
            {
                if (!wxnReadListEdit(e, def.extensions, wxnUserExtNormalize, bad))
                    warn.push_back(name + ": extensions is neither a list nor {add, remove}");
                for (const std::string& b : bad) warn.push_back(name + ": \"" + b + "\" is not an extension");
            }
            else if (key == "filenames")
            {
                auto fileName = [](const std::string& s) {
                    for (char c : s) if (c == '/' || c == '\\' || c == '*' || c == '?' || c == ':') return std::string();
                    return wxnLangLower(s);
                };
                if (!wxnReadListEdit(e, def.filenames, fileName, bad))
                    warn.push_back(name + ": filenames is neither a list nor {add, remove}");
                for (const std::string& b : bad) warn.push_back(name + ": \"" + b + "\" is not a file name");
            }
            else if (key == "firstLine")
            {
                std::string re;
                if (!wxnyaml::getText(e, re)) { warn.push_back(name + ": firstLine is not text"); continue; }
                try { std::regex probe(re, std::regex::ECMAScript); def.firstLine = re; }
                catch (const std::regex_error&) { warn.push_back(name + ": firstLine is not a regular expression"); }
            }
            else if (key == "comments")
            {
                if (!wxnyaml::isMap(e)) { warn.push_back(name + ": comments is not a map"); continue; }
                std::set<std::string> given;
                for (wxnyaml::Node c : e.children())
                {
                    const std::string ck = wxnyaml::keyOf(c);
                    if (!given.insert(ck).second) { warn.push_back(name + ": comments." + ck + " is given twice; the second is not used"); continue; }
                    // A null (`line:`) takes the form away, as '' and [] do.
                    const bool null = !wxnyaml::isScalar(c) && !wxnyaml::isMap(c) && !wxnyaml::isSeq(c);
                    if (ck == "line")
                    {
                        std::string tok;
                        if (wxnyaml::getText(c, tok) || null) { def.hasLineComment = true; def.lineComment = tok; }
                        else warn.push_back(name + ": comments.line is not text");
                    }
                    else if (ck == "block")
                    {
                        std::vector<std::string> pair;
                        std::string s;
                        bool allText = true;
                        if (wxnyaml::isSeq(c))
                            for (wxnyaml::Node p : c.children())
                            {
                                if (wxnyaml::getText(p, s) && !s.empty()) pair.push_back(s);
                                else allText = false;
                            }
                        if (null || (wxnyaml::isSeq(c) && allText && (pair.empty() || pair.size() == 2)))
                        {
                            def.hasBlockComment = true;
                            def.blockOpen  = pair.empty() ? std::string() : pair[0];
                            def.blockClose = pair.empty() ? std::string() : pair[1];
                        }
                        else warn.push_back(name + ": comments.block is not a pair [open, close]");
                    }
                    else warn.push_back(name + ": unknown comments entry \"" + ck + "\"");
                }
            }
            else if (key == "keywords")
            {
                const std::vector<std::string> names = wxnKeywordListNames(lexer, name);
                auto keep = [](const std::string& w) { return w; };
                auto readList = [&](wxnyaml::Node v, const std::string& list) {
                    WxnListEdit edit;
                    if (wxnReadListEdit(v, edit, keep, bad)) def.keywords[list] = edit;
                    else warn.push_back(name + ": keywords." + list + " is neither a list nor {add, remove}");
                };
                if (names.empty()) { warn.push_back(name + " has no keyword lists"); continue; }
                if (!wxnyaml::isMap(e) || wxnIsListEditMap(e)) { readList(e, wxnMainKeywordList(lexer)); continue; }
                std::set<std::string> lists;
                for (wxnyaml::Node l : e.children())
                {
                    const std::string list = wxnyaml::keyOf(l);
                    if (!lists.insert(list).second) { warn.push_back(name + ": keywords." + list + " is given twice; the second is not used"); continue; }
                    if (wxnDefIsNothing(l)) continue;
                    if (std::find(names.begin(), names.end(), list) == names.end())
                    {
                        std::string known;
                        for (const std::string& n : names) known += (known.empty() ? "" : ", ") + n;
                        warn.push_back(name + ": no keyword list \"" + list + "\" (it has " + known + ")");
                        continue;
                    }
                    readList(l, list);
                }
            }
            else warn.push_back(name + ": unknown entry \"" + key + "\"");
        }
        out.langs.push_back(std::move(def));
    }
    return true;
}

// ---- writing: the Style Configurator's User-defined keywords -------------------------------------------
//
// Set what `language`'s list `list` adds - the words in the Style Configurator's "User-defined keywords"
// box - in languages.yaml's `text`, and leave the rest of the file as it was: other entries, other lists,
// the list's removals. The file's opening comments are kept; comments further down are not, as rapidyaml
// cannot carry them. No words drops the addition, and an entry left empty goes with it. `mainList` is what
// the short form `keywords: [...]` stands for (wxnMainKeywordList). False, with *err set, for a file that
// does not parse, and for a list the file replaces whole: the box does not edit a replacement - unless
// `plainList`, for a user keyword group, where a list and an addition are the same thing (a group has no
// built-in words) and a list written as one stays one.
template <class Canonical>
bool wxnLangDefsSetAdded(std::string& text, const std::string& language, const std::string& list,
                         const std::string& mainList, const std::vector<std::string>& words, std::string* err,
                         Canonical&& canonical, bool plainList = false)
{
    using ryml::id_type;
    std::string t = text;
    wxnyaml::stripBom(t);
    wxnyaml::Doc doc;
    if (!wxnyaml::parse(t, doc, "languages.yaml")) { if (err) *err = doc.error; return false; }
    ryml::Tree& tree = doc.tree;
    const wxnyaml::Node r = doc.root();
    if (r.readable() && !r.is_map()) { if (err) *err = "the top level is not a map"; return false; }
    const id_type root = r.readable() ? r.id() : wxnyaml::resetToMap(tree).id();
    auto keyOf = [&](id_type n) { return tree.has_key(n) ? std::string(tree.key(n).str, tree.key(n).len) : std::string(); };
    auto child = [&](id_type parent, const std::string& k) { return tree.find_child(parent, wxnyaml::view(k)); };
    auto newMap = [&](id_type parent, const std::string& k) { return wxnyaml::addMap(tree.ref(parent), k).id(); };
    // A map under `k`; a null there (`languages:` with nothing under it) becomes one. NONE when something
    // else is there, which this does not overwrite.
    auto mapAt = [&](id_type parent, const std::string& k) -> id_type {
        const id_type c = child(parent, k);
        if (c == ryml::NONE) return newMap(parent, k);
        if (tree.is_map(c)) return c;
        if (tree.is_container(c) || (tree.has_val(c) && !tree.val_is_null(c))) return ryml::NONE;
        tree.remove(c);
        return newMap(parent, k);
    };
    auto isEditMap = [&](id_type n) {
        if (!tree.is_map(n) || !tree.has_children(n)) return false;
        for (id_type c = tree.first_child(n); c != ryml::NONE; c = tree.next_sibling(c))
            if (keyOf(c) != "add" && keyOf(c) != "remove") return false;
        return true;
    };
    // Nothing (`types:` with no value) says nothing, as the parser reads it: an empty map takes its place.
    auto isNothing = [&](id_type n) { return wxnDefIsNothing(tree.cref(n)); };
    auto mapInPlace = [&](id_type parent, id_type n) {
        const std::string k = keyOf(n);
        const id_type prev = tree.prev_sibling(n);
        tree.remove(n);
        const id_type m = newMap(parent, k);
        tree.move(m, parent, prev);
        return m;
    };

    const id_type langs = mapAt(root, "languages");
    if (langs == ryml::NONE) { if (err) *err = "\"languages\" is not a map"; return false; }
    id_type lang = ryml::NONE;
    for (id_type c = tree.first_child(langs); c != ryml::NONE && lang == ryml::NONE; c = tree.next_sibling(c))
        if (canonical(keyOf(c)) == language) lang = c;
    if (lang != ryml::NONE && isNothing(lang)) { if (words.empty()) return true; lang = mapInPlace(langs, lang); }
    if (lang == ryml::NONE) { if (words.empty()) return true; lang = newMap(langs, language); }
    else if (!tree.is_map(lang)) { if (err) *err = language + " is not a map"; return false; }

    // keywords: the short form for the main list is put under that list's name first.
    id_type kw = child(lang, "keywords");
    if (kw != ryml::NONE && isNothing(kw)) { if (words.empty()) return true; kw = mapInPlace(lang, kw); }
    if (kw != ryml::NONE && (!tree.is_map(kw) || isEditMap(kw)))
    {
        const id_type nested = newMap(lang, "keywords");
        tree.move(kw, nested, ryml::NONE);
        tree.set_key(kw, tree.copy_to_arena(wxnyaml::view(mainList)));
        kw = nested;
    }
    if (kw == ryml::NONE) { if (words.empty()) return true; kw = newMap(lang, "keywords"); }

    id_type l = child(kw, list);
    if (l != ryml::NONE && isNothing(l)) { if (words.empty()) return true; l = mapInPlace(kw, l); }
    auto fill = [&](wxnyaml::MutNode seq) { for (const std::string& w : words) wxnyaml::setText(wxnyaml::addItem(seq), w); };
    if (l != ryml::NONE && !tree.is_map(l))
    {
        if (!plainList) { if (err) *err = language + ": languages.yaml replaces the list " + list; return false; }
        const id_type prev = tree.prev_sibling(l);
        tree.remove(l);
        l = ryml::NONE;
        if (!words.empty())   // a group's list stays a list, where it was
        {
            wxnyaml::MutNode seq = wxnyaml::addSeq(tree.ref(kw), list);
            fill(seq);
            wxnyaml::setOneLine(seq);
            tree.move(seq.id(), kw, prev);
        }
    }
    else
    {
        if (l != ryml::NONE && !isEditMap(l) && tree.has_children(l)) { if (err) *err = language + ": keywords." + list + " is not {add, remove}"; return false; }
        if (l == ryml::NONE && words.empty()) return true;
        if (l == ryml::NONE) l = newMap(kw, list);
        if (const id_type a = child(l, "add"); a != ryml::NONE) tree.remove(a);
        if (!words.empty())
        {
            wxnyaml::MutNode add = wxnyaml::addSeq(tree.ref(l), "add");
            fill(add);
            tree.move(add.id(), l, ryml::NONE);   // "add" before "remove"
        }
        wxnyaml::setOneLine(tree.ref(l));
        if (!tree.has_children(l)) tree.remove(l);
    }
    // An entry left with nothing in it goes.
    if (!tree.has_children(kw)) tree.remove(kw);
    if (!tree.has_children(lang)) tree.remove(lang);

    std::string body;
    if (!wxnyaml::emit(tree, body)) { if (err) *err = "the file could not be written"; return false; }
    text = wxnyaml::leadingComments(t) + body;
    return true;
}

// Lay `overlay` - languages.yaml text, such as an import makes - over `text`, the user's languages.yaml:
// a language the overlay names takes its entries (extensions, filenames, firstLine; each of comments'
// line and block; each keyword list), and keeps every entry the overlay leaves out. Other languages, and
// anything else in the file, stay as they were; the file's opening comments are kept. False, with *err,
// when either does not parse.
template <class Canonical, class LexerOf>
bool wxnLangDefsMerge(std::string& text, const std::string& overlay, std::string* err, Canonical&& canonical,
                      LexerOf&& lexerOf)
{
    using ryml::id_type;
    std::string t = text;
    wxnyaml::stripBom(t);
    wxnyaml::Doc base, over;
    if (!wxnyaml::parse(t, base, "languages.yaml")) { if (err) *err = base.error; return false; }
    if (!wxnyaml::parse(overlay, over, "imported definitions")) { if (err) *err = over.error; return false; }
    const wxnyaml::Node o = wxnyaml::child(over.root(), "languages");
    if (!wxnyaml::isMap(o) || !o.has_children()) return true;   // nothing to lay over
    ryml::Tree& tree = base.tree;
    const wxnyaml::Node r = base.root();
    if (r.readable() && !r.is_map()) { if (err) *err = "the top level is not a map"; return false; }
    const id_type root = r.readable() ? r.id() : wxnyaml::resetToMap(tree).id();
    auto keyOf = [&](id_type n) { return tree.has_key(n) ? std::string(tree.key(n).str, tree.key(n).len) : std::string(); };
    auto child = [&](id_type parent, const std::string& k) { return tree.find_child(parent, wxnyaml::view(k)); };
    auto newMap = [&](id_type parent, const std::string& k) { return wxnyaml::addMap(tree.ref(parent), k).id(); };
    auto mapAt = [&](id_type parent, const std::string& k) -> id_type {
        const id_type c = child(parent, k);
        if (c != ryml::NONE && tree.is_map(c)) return c;
        if (c != ryml::NONE) tree.remove(c);   // a null there, or something the overlay replaces
        return newMap(parent, k);
    };
    auto isEditMap = [&](id_type n) {
        if (!tree.is_map(n) || !tree.has_children(n)) return false;
        for (id_type c = tree.first_child(n); c != ryml::NONE; c = tree.next_sibling(c))
            if (keyOf(c) != "add" && keyOf(c) != "remove") return false;
        return true;
    };
    // Put a copy of the overlay's node `src` under `parent`, as `k`, in place of the child of that key.
    auto put = [&](id_type parent, wxnyaml::Node src, const std::string& k) {
        const id_type old = child(parent, k);
        const id_type after = old != ryml::NONE ? tree.prev_sibling(old) : tree.last_child(parent);
        if (old != ryml::NONE) tree.remove(old);
        const id_type copy = tree.duplicate(&over.tree, src.id(), parent, after);
        tree.set_key(copy, tree.copy_to_arena(wxnyaml::view(k)));
    };
    const id_type langs = mapAt(root, "languages");
    for (wxnyaml::Node ol : o.children())
    {
        const std::string name = canonical(wxnyaml::keyOf(ol));
        if (name.empty() || !wxnyaml::isMap(ol)) continue;
        id_type lang = ryml::NONE;
        for (id_type c = tree.first_child(langs); c != ryml::NONE && lang == ryml::NONE; c = tree.next_sibling(c))
            if (canonical(keyOf(c)) == name) lang = c;
        if (lang != ryml::NONE && !tree.is_map(lang)) { tree.remove(lang); lang = ryml::NONE; }
        if (lang == ryml::NONE) lang = newMap(langs, name);
        for (wxnyaml::Node e : ol.children())
        {
            const std::string k = wxnyaml::keyOf(e);
            const id_type mine = child(lang, k);
            if (k == "comments" && wxnyaml::isMap(e) && mine != ryml::NONE && tree.is_map(mine))
            {
                for (wxnyaml::Node c : e.children()) put(mine, c, wxnyaml::keyOf(c));
                continue;
            }
            // Keyword lists one by one, either side's short form standing for the main list.
            if (k == "keywords" && mine != ryml::NONE && !wxnDefIsNothing(tree.cref(mine)))
            {
                const std::string mainList = wxnMainKeywordList(lexerOf(name));
                id_type kw = mine;
                if (!tree.is_map(kw) || isEditMap(kw))
                {   // the user's short form: put it under its list's name first
                    const id_type nested = newMap(lang, "keywords");
                    tree.move(kw, nested, ryml::NONE);
                    tree.set_key(kw, tree.copy_to_arena(wxnyaml::view(mainList)));
                    kw = nested;
                }
                if (wxnyaml::isMap(e) && !wxnIsListEditMap(e))
                    for (wxnyaml::Node l : e.children()) put(kw, l, wxnyaml::keyOf(l));
                else put(kw, e, mainList);
                continue;
            }
            put(lang, e, k);
        }
    }
    std::string body;
    if (!wxnyaml::emit(tree, body)) { if (err) *err = "the file could not be written"; return false; }
    text = wxnyaml::leadingComments(t) + body;
    return true;
}

// What Settings > Edit Language Definitions starts a new languages.yaml with: how to write one, as
// comments, over an empty `languages:`.
inline const char* wxnLanguagesYamlTemplate()
{
    return
        "# languages.yaml - your changes to wxNote's languages: which files open as each, how each is\n"
        "# commented, and the keyword lists its highlighting and completion use. What is not here stays as\n"
        "# wxNote has it; Settings > Style Configurator shows wxNote's own (Default ext., Default keywords).\n"
        "#\n"
        "# Lists take two forms: a list replaces wxNote's own, {add: [...], remove: [...]} changes it.\n"
        "# A language is named as the Language menu names it.\n"
        "#\n"
        "# languages:\n"
        "#   C++:\n"
        "#     extensions: {add: [ipp, tpp]}            # without the dot\n"
        "#     filenames: [conanfile.txt]               # whole file names, any case\n"
        "#     firstLine: '^//.*-\\*-\\s*c\\+\\+'           # a regular expression the first line can match\n"
        "#     comments: {line: '//', block: ['/*', '*/']}   # '' or [] takes a form away\n"
        "#     keywords:                                # each list by the name the Style Configurator shows\n"
        "#       types: {add: [size_t, ssize_t]}\n"
        "#       userKeywords1: [Q_OBJECT, emit]        # coloured by the theme's USER KEYWORDS 1\n"
        "#   Python:\n"
        "#     keywords: {add: [match, case]}           # short for the language's \"keywords\" list\n"
        "languages:\n";
}

// ---- applying the definitions ----------------------------------------------------------------------

inline std::vector<std::string> wxnSplitWords(const char* words)
{
    std::vector<std::string> out;
    std::string cur;
    for (const char* p = words ? words : ""; ; ++p)
    {
        if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == '\0') { if (!cur.empty()) out.push_back(cur); cur.clear(); }
        else cur += *p;
        if (!*p) break;
    }
    return out;
}
inline std::string wxnJoinWords(const std::vector<std::string>& words)
{
    std::string s;
    for (const std::string& w : words) { if (!s.empty()) s += ' '; s += w; }
    return s;
}

// The built-in words of one of `language`'s lists (keywords.h), slot by slot.
inline std::map<int, std::string> wxnBuiltinKeywordLists(const std::string& language)
{
    std::map<int, std::string> out;
    wxnForEachKeywordList(language, [&](const WxnKeywordList& k) { out[k.slot] = k.words; });
    return out;
}

// What `language` (on `lexer`) hands each keyword slot once `def` (may be null) is applied: slot -> words.
// A slot the file empties is in the map with "", so the caller clears what an earlier document set.
inline std::map<int, std::string> wxnEffectiveKeywordLists(const std::string& language, const std::string& lexer,
                                                           const WxnLangDef* def)
{
    std::map<int, std::string> out = wxnBuiltinKeywordLists(language);
    if (!def) return out;
    for (const WxnKeywordSetName* s : wxnKeywordSetsOf(lexer))
    {
        const auto e = def->keywords.find(s->name);
        if (e == def->keywords.end()) continue;
        const auto b = out.find(s->slot);
        out[s->slot] = wxnJoinWords(e->second.apply(wxnSplitWords(b == out.end() ? "" : b->second.c_str())));
    }
    return out;
}

// The user keyword groups of `language` that have words, as (group, words).
inline std::vector<std::pair<WxnSubstyleGroup, std::string>> wxnUserKeywordGroups(const std::string& language,
                                                                                   const std::string& lexer,
                                                                                   const WxnLangDef* def)
{
    std::vector<std::pair<WxnSubstyleGroup, std::string>> out;
    if (!def) return out;
    for (const WxnSubstyleGroup& g : wxnSubstyleGroupsOf(lexer, language))
    {
        const auto e = def->keywords.find(g.name);
        if (e == def->keywords.end()) continue;
        const std::string words = wxnJoinWords(e->second.apply({}));
        if (!words.empty()) out.emplace_back(g, words);
    }
    return out;
}

// `base` with the user's comments for the language applied. The strings point into `def`, which must
// outlive the result. A token changed from the built-in one is recognised with or without a following
// space and in its own case only, unless it is word-shaped ("rem"), which comments only before a space.
inline WxnCommentStyle wxnApplyCommentDef(WxnCommentStyle base, const WxnLangDef* def)
{
    if (!def) return base;
    if (def->hasLineComment)
    {
        const std::string was = base.line ? base.line : "";
        base.line = def->lineComment.c_str();
        if (wxnLangLower(was) != wxnLangLower(def->lineComment))
        {
            const char last = def->lineComment.empty() ? ' ' : def->lineComment.back();
            base.lineNeedsSpace = (last >= 'a' && last <= 'z') || (last >= 'A' && last <= 'Z') || (last >= '0' && last <= '9');
            base.lineCaseless = false;
        }
    }
    if (def->hasBlockComment)
    {
        base.blockOpen  = def->blockOpen.c_str();
        base.blockClose = def->blockClose.c_str();
    }
    return base;
}

// What languages.yaml says about file names, as detection reads it (lang_detect.h).
inline WxnLangFileRules wxnLangFileRulesFrom(const WxnLangDefs& defs)
{
    WxnLangFileRules r;
    for (const WxnLangDef& d : defs.langs)
    {
        for (const std::string& e : d.extensions.words) r.extToLang.emplace(e, d.name);   // the first language to claim it
        for (const std::string& n : d.filenames.words)  r.nameToLang.emplace(n, d.name);
        WxnLangFileRules::Off& off = r.off[d.name];
        off.allExtensions = d.extensions.replace;
        off.allNames      = d.filenames.replace;
        off.extensions.insert(d.extensions.remove.begin(), d.extensions.remove.end());
        off.names.insert(d.filenames.remove.begin(), d.filenames.remove.end());
        if (!d.firstLine.empty())
            try { r.firstLine.emplace_back(std::regex(d.firstLine, std::regex::ECMAScript), d.name); }
            catch (const std::regex_error&) {}
    }
    return r;
}
