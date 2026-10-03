// SPDX-License-Identifier: Apache-2.0
//
// wxNote - settings_store.h: the stores behind settings.yaml and state.yaml.
// Copyright 2026 The wxNote Authors.
//
// docs/SETTINGS_DESIGN.md is the reference. In short:
//
//   SettingsFile  settings.yaml, which belongs to the user. Kept AS TEXT: reads go through the parsed
//                 tree, and a change rewrites only the line (or block) of the setting it touches, so
//                 the user's comments, order and blank lines survive - rapidyaml cannot carry comments
//                 through a parse and an emit. Every edit is checked by parsing the result and comparing
//                 it with what was meant; where the line editor does not understand the layout, the file
//                 is written out whole instead (losing comments, never values). A file that does not
//                 parse is never written over.
//   StateFile     state.yaml, which belongs to wxNote: '/'-separated paths in a flat model, written
//                 whole. Saving lays only the subtrees this process changed over what is on disk, so two
//                 wxNote processes do not undo each other's recent files or recovery entries.
//
// No wx and no file I/O: the caller moves the bytes (atomically) and the tests drive both from strings.

#pragma once

#include "yaml_io.h"

#include <algorithm>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace wxnsettings {

using wxnyaml::Node;

// ================================================================================================
// settings.yaml
// ================================================================================================

namespace detail {

// One top-level entry of a settings file, as byte ranges of its text.
struct Entry
{
    std::string key;
    size_t begin = 0, end = 0;              // the key line and every line of the value, newlines included
    size_t keyLineEnd = 0;                  // one past the key line's newline
    size_t valueBegin = 0, valueEnd = 0;    // the value on the key line, when it is a one-line scalar
    bool   inlineValue = false;
};

inline bool blank(const std::string& t, size_t b, size_t e)
{
    for (size_t i = b; i < e; ++i)
        if (t[i] != ' ' && t[i] != '\t' && t[i] != '\r') return false;
    return true;
}

// Split settings text into its top-level entries. `understood` goes false on any layout this line
// editor does not take apart - a top-level list or flow map, anchors, tags, directives, a second
// document, text indented before the first key - and the caller then writes the file out whole.
inline std::vector<Entry> scanEntries(const std::string& t, bool& understood)
{
    std::vector<Entry> out;
    understood = true;
    bool inEntry = false, sawEntry = false;
    for (size_t ls = 0; ls < t.size();)
    {
        size_t le = t.find('\n', ls);
        const size_t next = le == std::string::npos ? t.size() : le + 1;
        if (le == std::string::npos) le = t.size();
        const size_t lineEnd = (le > ls && t[le - 1] == '\r') ? le - 1 : le;   // the line's text is [ls, lineEnd)
        const size_t at = ls;
        ls = next;

        if (blank(t, at, lineEnd)) continue;                        // blank lines belong to nobody
        const char c0 = t[at];
        if (c0 == ' ' || c0 == '\t')                                // the value of the entry above, continued
        {
            if (!inEntry) { understood = false; return out; }
            out.back().end = next;
            continue;
        }
        if (c0 == '#') { inEntry = false; continue; }               // a comment at column 0 ends the entry above
        if (t.compare(at, 3, "---") == 0 && (lineEnd - at == 3 || t[at + 3] == ' ' || t[at + 3] == '\t'))
        {
            if (sawEntry) { understood = false; return out; }       // a second document
            inEntry = false;
            continue;
        }

        Entry e;
        e.begin = at;
        e.end = e.keyLineEnd = next;
        size_t p = at;
        if (c0 == '"' || c0 == '\'')                                // a quoted key
        {
            std::string key;
            bool closed = false;
            for (++p; p < lineEnd;)
            {
                const char c = t[p];
                if (c0 == '\'' && c == '\'')
                {
                    if (p + 1 < lineEnd && t[p + 1] == '\'') { key += '\''; p += 2; continue; }
                    closed = true; ++p; break;
                }
                if (c0 == '"' && c == '\\')
                {
                    if (p + 1 < lineEnd && (t[p + 1] == '"' || t[p + 1] == '\\')) { key += t[p + 1]; p += 2; continue; }
                    understood = false; return out;                 // another escape: not worth decoding here
                }
                if (c0 == '"' && c == '"') { closed = true; ++p; break; }
                key += c;
                ++p;
            }
            while (p < lineEnd && (t[p] == ' ' || t[p] == '\t')) ++p;
            if (!closed || p >= lineEnd || t[p] != ':') { understood = false; return out; }
            e.key = key;
            ++p;
        }
        else
        {
            if (std::strchr("-[]{}?:&*!|>%@`,.", c0)) { understood = false; return out; }
            size_t colon = std::string::npos;
            for (size_t i = at; i < lineEnd; ++i)
            {
                if (t[i] == '#' && (t[i - 1] == ' ' || t[i - 1] == '\t')) break;
                if (t[i] == ':' && (i + 1 == lineEnd || t[i + 1] == ' ' || t[i + 1] == '\t')) { colon = i; break; }
            }
            if (colon == std::string::npos) { understood = false; return out; }
            size_t ke = colon;
            while (ke > at && (t[ke - 1] == ' ' || t[ke - 1] == '\t')) --ke;
            e.key = t.substr(at, ke - at);
            p = colon + 1;
        }

        while (p < lineEnd && (t[p] == ' ' || t[p] == '\t')) ++p;
        e.valueBegin = e.valueEnd = p;
        if (p < lineEnd && t[p] != '#')
        {
            const char v0 = t[p];
            if (v0 == '"' || v0 == '\'')
            {
                size_t i = p + 1;
                bool closed = false;
                while (i < lineEnd)
                {
                    if (v0 == '"' && t[i] == '\\') { i += 2; continue; }
                    if (t[i] == v0)
                    {
                        if (v0 == '\'' && i + 1 < lineEnd && t[i + 1] == '\'') { i += 2; continue; }
                        closed = true;
                        ++i;
                        break;
                    }
                    ++i;
                }
                if (closed) { e.valueEnd = i; e.inlineValue = true; }  // else it runs on: replaced as a block
            }
            else if (!std::strchr("[{|>&*!", v0))                   // flow collections, block scalars, anchors,
            {                                                       // aliases and tags are replaced as a block
                size_t i = p;
                while (i < lineEnd && !(t[i] == '#' && (t[i - 1] == ' ' || t[i - 1] == '\t'))) ++i;
                while (i > p && (t[i - 1] == ' ' || t[i - 1] == '\t')) --i;
                e.valueEnd = i;
                e.inlineValue = true;
            }
        }
        out.push_back(e);
        inEntry = sawEntry = true;
    }
    for (Entry& e : out)                                            // a plain value continued on the next
        if (e.inlineValue && e.end != e.keyLineEnd) e.inlineValue = false;   // lines is not one line after all
    return out;
}

// The top-level entries of a parsed document, key -> the entry written out on its own. The first of
// two entries with the same key wins, as it does for every reader.
inline std::map<std::string, std::string> emittedEntries(Node root)
{
    std::map<std::string, std::string> out;
    if (wxnyaml::isMap(root))
        for (Node c : root.children()) out.emplace(wxnyaml::keyOf(c), wxnyaml::emitNode(c));
    return out;
}

// Does `after` hold exactly what `before` held, apart from `id`?
inline bool sameApartFrom(const wxnyaml::Doc& before, const wxnyaml::Doc& after, const std::string& id)
{
    std::map<std::string, std::string> a = emittedEntries(before.root()), b = emittedEntries(after.root());
    a.erase(id);
    b.erase(id);
    return a == b;
}

inline std::string newlineOf(const std::string& t)
{
    const size_t lf = t.find('\n');
    return lf != std::string::npos && lf > 0 && t[lf - 1] == '\r' ? "\r\n" : "\n";
}

inline std::string withNewlines(std::string s, const std::string& nl)
{
    if (nl == "\n") return s;
    std::string out;
    out.reserve(s.size() + s.size() / 16);
    for (const char c : s)
    {
        if (c == '\n') out += nl;
        else out += c;
    }
    return out;
}

inline std::string namespaceOf(const std::string& id) { return id.substr(0, id.find('.')); }

}   // namespace detail

class SettingsFile
{
public:
    // Take the file's text - at startup, and again when it changed on disk. False when it does not
    // parse, or is no map of settings: reads then find nothing (every setting at its default) and edits
    // are refused until a usable text arrives, since writing would replace whatever the user is in the
    // middle of fixing.
    bool load(std::string text)
    {
        m_text = std::move(text);
        wxnyaml::stripBom(m_text);                  // so the line editor sees the first key as it is
        m_dirty = false;
        if (!wxnyaml::parse(m_text, m_doc, "settings.yaml")) return false;
        // A second document ("---" and more after the first) is not read, and a whole-file rewrite would
        // drop it: refuse to edit such a file rather than lose what it holds.
        const ryml::ConstNodeRef top = m_doc.tree.size() ? m_doc.tree.crootref() : ryml::ConstNodeRef();
        if (top.readable() && top.is_stream())
            for (size_t i = 1; i < top.num_children(); ++i)
            {
                const ryml::ConstNodeRef d = top.child(i);
                if (d.has_children() || (d.has_val() && !d.val_is_null()))
                {
                    m_doc.error = "more than one YAML document (\"---\"): only one is a settings file";
                    return false;
                }
            }
        const Node root = m_doc.root();
        if (!root.readable() || wxnyaml::isMap(root)) return true;
        m_doc.error = "expected 'id: value' lines at the top level";   // a list or a lone value: no settings file
        return false;
    }
    // The file is there but could not be read whole (another program holds it, or is writing it): like a
    // file that does not parse, nothing is read from it and every edit is refused until a load succeeds.
    void loadUnreadable(const std::string& why)
    {
        m_text.clear();
        m_dirty = false;
        m_doc = wxnyaml::Doc();
        m_doc.error = why;
    }
    const std::string& text() const { return m_text; }
    bool ok() const { return m_doc.ok(); }
    const std::string& error() const { return m_doc.error; }
    bool dirty() const { return m_dirty; }
    void markSaved() { m_dirty = false; }

    Node get(const std::string& id) const { return wxnyaml::child(m_doc.root(), id); }
    // `id` under `languages: <lang>:`.
    Node getForLanguage(const std::string& lang, const std::string& id) const
    {
        return wxnyaml::child(wxnyaml::child(wxnyaml::child(m_doc.root(), "languages"), lang), id);
    }

    // Set `id` to a one-line value: `yaml` is its spelling after "id: " (wxnyaml::scalarYaml() for
    // text, "true", "42") and `expect` the text a reader must get back. False if refused.
    bool set(const std::string& id, const std::string& yaml, const std::string& expect)
    {
        const std::string nl = detail::newlineOf(m_text);
        const std::string line = wxnyaml::keyYaml(id) + ": " + yaml + nl;
        return edit(id, line, yaml, [&](const wxnyaml::Doc& d) {
            std::string got;
            return wxnyaml::getText(wxnyaml::child(d.root(), id), got) && got == expect;
        });
    }

    // Set `id` to a map of text values (files.associations). An empty map removes the setting.
    bool setMap(const std::string& id, const std::vector<std::pair<std::string, std::string>>& entries)
    {
        if (entries.empty()) return remove(id);
        const std::string nl = detail::newlineOf(m_text);
        std::string block = wxnyaml::keyYaml(id) + ":" + nl;
        for (const auto& kv : entries) block += "  " + wxnyaml::keyYaml(kv.first) + ": " + wxnyaml::scalarYaml(kv.second) + nl;
        return edit(id, block, std::string(), [&](const wxnyaml::Doc& d) {
            const Node m = wxnyaml::child(d.root(), id);
            if (!wxnyaml::isMap(m) || m.num_children() != entries.size()) return false;
            size_t i = 0;
            for (Node c : m.children())
            {
                std::string v;
                if (wxnyaml::keyOf(c) != entries[i].first || !wxnyaml::getText(c, v) || v != entries[i].second) return false;
                ++i;
            }
            return true;
        });
    }

    // Drop `id`, back to its default. True when it is gone, or never was there.
    bool remove(const std::string& id)
    {
        if (!m_doc.ok()) return false;
        if (!get(id).readable() && !hasEntry(id)) return true;
        return edit(id, std::string(), std::string(), [&](const wxnyaml::Doc& d) { return !wxnyaml::child(d.root(), id).readable(); });
    }

private:
    std::string   m_text;
    wxnyaml::Doc  m_doc;
    bool          m_dirty = false;

    bool hasEntry(const std::string& id) const
    {
        bool understood = false;
        for (const detail::Entry& e : detail::scanEntries(m_text, understood))
            if (e.key == id) return true;
        return false;
    }

    // Replace `id`'s entry with `entry` (complete lines; "" deletes it) - in place when the line editor
    // understands the file, else by writing it out whole - and keep the result only if `check` passes
    // and every other entry reads back exactly as before. `inlineValue`, when set, is the new value's
    // spelling for an in-place swap on the key line, which keeps that line's comment.
    bool edit(const std::string& id, const std::string& entry, const std::string& inlineValue,
              const std::function<bool(const wxnyaml::Doc&)>& check)
    {
        if (!m_doc.ok()) return false;   // unusable (see load): leave the user's text alone
        const std::string nl = detail::newlineOf(m_text);

        bool understood = false;
        const std::vector<detail::Entry> entries = detail::scanEntries(m_text, understood);
        std::string next = m_text;
        if (understood)
        {
            const detail::Entry* found = nullptr;
            const detail::Entry* sameNamespace = nullptr;
            for (const detail::Entry& e : entries)
            {
                if (!found && e.key == id) found = &e;
                if (detail::namespaceOf(e.key) == detail::namespaceOf(id)) sameNamespace = &e;
            }
            if (found && found->inlineValue && !inlineValue.empty())
                next.replace(found->valueBegin, found->valueEnd - found->valueBegin, inlineValue);
            else if (found)
                next.replace(found->begin, found->end - found->begin, entry);
            else if (!entry.empty())
            {
                size_t pos = sameNamespace ? sameNamespace->end : next.size();
                if (pos == next.size() && !next.empty() && next.back() != '\n') { next += nl; pos = next.size(); }
                next.insert(pos, entry);
            }
        }

        if (understood && next == m_text) return check(m_doc);    // already so: nothing to write

        wxnyaml::Doc after;
        if (!understood || !wxnyaml::parse(next, after) || !check(after) || !detail::sameApartFrom(m_doc, after, id))
        {
            next = rewriteWhole(id, entry, nl);
            if (!wxnyaml::parse(next, after) || !check(after) || !detail::sameApartFrom(m_doc, after, id)) return false;
        }
        m_text = std::move(next);
        m_doc = std::move(after);
        m_dirty = true;
        return true;
    }

    // The file written out whole: its header comment, then every entry in its old order with `id`'s
    // replaced by `entry` (or dropped, or added last).
    std::string rewriteWhole(const std::string& id, const std::string& entry, const std::string& nl) const
    {
        std::string out = wxnyaml::leadingComments(m_text);
        bool placed = false;
        const Node root = m_doc.root();
        if (wxnyaml::isMap(root))
            for (Node c : root.children())
            {
                if (wxnyaml::keyOf(c) == id)
                {
                    if (!placed) out += entry;
                    placed = true;
                    continue;
                }
                out += detail::withNewlines(wxnyaml::emitNode(c), nl);
            }
        if (!placed) out += entry;
        return out;
    }
};

// ================================================================================================
// state.yaml
// ================================================================================================

class StateFile
{
public:
    // Take the file's text. False when it does not parse: the store then starts empty, and the next
    // save replaces the file - it is wxNote's own scratch memory, with nothing a user wrote in it.
    bool load(const std::string& text)
    {
        m_leaves.clear();
        m_touched.clear();
        wxnyaml::Doc d;
        std::string t = text;
        wxnyaml::stripBom(t);
        if (!wxnyaml::parse(t, d, "state.yaml")) return false;
        flatten(d.root(), std::string(), m_leaves, 0);
        return true;
    }

    bool getBool(const std::string& path, bool fallback) const
    {
        const Leaf* l = scalar(path);
        if (!l) return fallback;
        if (l->text == "true") return true;
        if (l->text == "false") return false;
        return fallback;
    }
    long long getInt(const std::string& path, long long fallback) const
    {
        const Leaf* l = scalar(path);
        long long v = 0;
        return l && parseInteger(l->text, v) ? v : fallback;
    }
    std::string getText(const std::string& path, const std::string& fallback = std::string()) const
    {
        const Leaf* l = scalar(path);
        return l ? l->text : fallback;
    }
    std::vector<std::string> getList(const std::string& path) const
    {
        auto it = m_leaves.find(path);
        return it != m_leaves.end() && it->second.list ? it->second.items : std::vector<std::string>();
    }
    // The names directly under `path` - "recovery/entries" gives the entries' ids - sorted.
    std::vector<std::string> children(const std::string& path) const
    {
        std::vector<std::string> out;
        const std::string prefix = path.empty() ? std::string() : path + "/";
        for (auto it = m_leaves.lower_bound(prefix); it != m_leaves.end() && it->first.compare(0, prefix.size(), prefix) == 0; ++it)
        {
            const std::string rest = it->first.substr(prefix.size());
            const std::string name = rest.substr(0, rest.find('/'));
            if (!name.empty() && (out.empty() || out.back() != name)) out.push_back(name);
        }
        return out;
    }

    void setBool(const std::string& path, bool v)        { put(path, Leaf{ false, true, v ? "true" : "false", {} }); }
    void setInt(const std::string& path, long long v)    { put(path, Leaf{ false, true, std::to_string(v), {} }); }
    void setText(const std::string& path, const std::string& v) { put(path, Leaf{ false, false, v, {} }); }
    void setList(const std::string& path, const std::vector<std::string>& v) { put(path, Leaf{ true, false, std::string(), v }); }
    // Drop the value at `path`, or everything under it.
    void remove(const std::string& path)
    {
        eraseSubtree(m_leaves, path);
        touch(path);
    }

    bool dirty() const { return !m_touched.empty(); }

    // The file as it should be written: `disk` (what is in it now; "" for none) with every subtree this
    // store changed since the last save laid over it, in the order the changes were made - so a key that
    // changed shape ("a/b", then "a") ends as it was set last. This store then holds the merged state
    // too, so a value another wxNote process saved in the meantime is not undone - and becomes visible
    // here. The changes stay pending until markSaved(): a write that fails is retried by the next save.
    std::string mergeForSave(const std::string& disk)
    {
        StateFile merged;
        merged.load(disk);                       // unreadable on disk: nothing of it survives
        for (const std::string& t : m_touched)
        {
            eraseSubtree(merged.m_leaves, t);
            // Everything starting with t sorts together, but "a b" lands between "a" and "a/x": test each.
            bool wrote = false;
            for (auto it = m_leaves.lower_bound(t); it != m_leaves.end() && it->first.compare(0, t.size(), t) == 0; ++it)
                if (it->first.size() == t.size() || it->first[t.size()] == '/')
                {
                    if (!wrote) eraseAncestors(merged.m_leaves, t);   // only a value written displaces one above it
                    merged.m_leaves[it->first] = it->second;
                    wrote = true;
                }
        }
        m_leaves.swap(merged.m_leaves);
        return text();
    }
    // The text mergeForSave returned is on disk now.
    void markSaved() { m_touched.clear(); }

    // Everything, as YAML, behind a one-line header.
    std::string text() const
    {
        ryml::Tree t;
        wxnyaml::MutNode root = wxnyaml::resetToMap(t);
        for (const auto& kv : m_leaves)
        {
            wxnyaml::MutNode parent = root;
            size_t from = 0;
            for (size_t slash; (slash = kv.first.find('/', from)) != std::string::npos; from = slash + 1)
            {
                const std::string seg = kv.first.substr(from, slash - from);
                wxnyaml::MutNode c = parent.find_child(wxnyaml::view(seg));
                parent = c.readable() ? c : wxnyaml::addMap(parent, seg);
            }
            wxnyaml::MutNode leaf = wxnyaml::addKey(parent, kv.first.substr(from));
            const Leaf& l = kv.second;
            if (l.list)
            {
                leaf |= ryml::SEQ;
                for (const std::string& item : l.items) wxnyaml::setText(wxnyaml::addItem(leaf), item);
            }
            else if (l.plain && plainSafe(l.text))
            {
                leaf.set_val(wxnyaml::keep(t, l.text));
                leaf.set_val_style(ryml::VAL_PLAIN);
            }
            else
                wxnyaml::setText(leaf, l.text);
        }
        std::string out;
        if (!wxnyaml::emit(t, out)) return std::string();
        return "# wxNote's memory between runs: window, recent files, last session. Rewritten whole -\n"
               "# your own settings go in settings.yaml.\n" + out;
    }

private:
    struct Leaf
    {
        bool list = false;
        bool plain = false;                 // a number or a bool, written unquoted
        std::string text;
        std::vector<std::string> items;
    };
    std::map<std::string, Leaf> m_leaves;   // path -> value
    std::vector<std::string>    m_touched;  // paths set or removed since the last save, oldest first

    const Leaf* scalar(const std::string& path) const
    {
        auto it = m_leaves.find(path);
        return it != m_leaves.end() && !it->second.list ? &it->second : nullptr;
    }

    static bool parseInteger(const std::string& s, long long& v)
    {
        if (s.empty()) return false;
        size_t i = (s[0] == '-' || s[0] == '+') ? 1 : 0;
        if (i == s.size()) return false;
        for (size_t k = i; k < s.size(); ++k)
            if (s[k] < '0' || s[k] > '9') return false;
        if (s.size() - i > 18) return false;
        v = std::stoll(s);
        return true;
    }
    static bool plainSafe(const std::string& s)
    {
        long long v;
        return s == "true" || s == "false" || parseInteger(s, v);
    }

    static void eraseSubtree(std::map<std::string, Leaf>& m, const std::string& path)
    {
        m.erase(path);
        const std::string prefix = path + "/";
        for (auto it = m.lower_bound(prefix); it != m.end() && it->first.compare(0, prefix.size(), prefix) == 0;) it = m.erase(it);
    }
    // A value at "a" and one at "a/b" cannot both be written: setting either drops the other.
    static void eraseAncestors(std::map<std::string, Leaf>& m, const std::string& path)
    {
        for (size_t slash = path.find('/'); slash != std::string::npos; slash = path.find('/', slash + 1)) m.erase(path.substr(0, slash));
    }
    void put(const std::string& path, Leaf l)
    {
        if (path.empty()) return;
        eraseSubtree(m_leaves, path);
        eraseAncestors(m_leaves, path);
        m_leaves[path] = std::move(l);
        touch(path);
    }
    // Record `path` as changed, as the most recent change.
    void touch(const std::string& path)
    {
        m_touched.erase(std::remove(m_touched.begin(), m_touched.end(), path), m_touched.end());
        m_touched.push_back(path);
    }

    static void flatten(Node n, const std::string& prefix, std::map<std::string, Leaf>& out, int depth)
    {
        if (!n.readable() || depth > 32) return;
        if (wxnyaml::isMap(n))
        {
            for (Node c : n.children())
            {
                const std::string k = wxnyaml::keyOf(c);
                if (k.empty() || k.find('/') != std::string::npos) continue;   // not representable as a path
                flatten(c, prefix.empty() ? k : prefix + "/" + k, out, depth + 1);
            }
            return;
        }
        if (prefix.empty()) return;
        if (wxnyaml::isSeq(n))
        {
            Leaf l;
            l.list = true;
            for (Node c : n.children())
            {
                std::string s;
                if (wxnyaml::getText(c, s)) l.items.push_back(s);
            }
            out[prefix] = std::move(l);
            return;
        }
        std::string s;
        if (wxnyaml::getText(n, s)) out[prefix] = Leaf{ false, !n.is_val_quoted(), s, {} };
    }
};

}   // namespace wxnsettings
