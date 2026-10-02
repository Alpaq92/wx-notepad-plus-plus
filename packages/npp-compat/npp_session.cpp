// SPDX-License-Identifier: GPL-3.0-or-later
//
// npp-compat - Notepad++'s session file (see npp_session.h).
// Copyright 2026 The wxNote Authors. See LICENSE (GPL-3.0-or-later).

#include "npp_session.h"
#include "npp_xml.h"
#include "yaml_io.h"

#include <cstdlib>

namespace nppcompat {

namespace {

// A non-negative decimal attribute, or `fallback`.
long long number(const std::string& s, long long fallback)
{
    if (s.empty() || s.size() > 18 || s.find_first_not_of("0123456789") != std::string::npos) return fallback;
    return std::strtoll(s.c_str(), nullptr, 10);
}

// Text in an attribute value: the five markup characters escaped, and the control characters XML 1.0
// cannot hold at all (anything below a space but tab, newline and carriage return) left out.
std::string attrEscape(const std::string& s)
{
    std::string o;
    o.reserve(s.size());
    for (const char c : s)
        switch (c)
        {
            case '&':  o += "&amp;"; break;
            case '<':  o += "&lt;"; break;
            case '>':  o += "&gt;"; break;
            case '"':  o += "&quot;"; break;
            case '\t': o += "&#9;"; break;
            case '\n': o += "&#10;"; break;
            case '\r': o += "&#13;"; break;
            default:   if (static_cast<unsigned char>(c) >= 0x20) o += c;
        }
    return o;
}

}   // namespace

bool sessionFromNpp(const std::string& xml, NppSession& out, std::string* err)
{
    out = NppSession();
    XmlElement root;
    if (!parseXml(xml, root, err)) return false;
    const XmlElement* session = root.child("Session");
    if (!session) session = &root;                 // tolerate a bare <mainView>/<subView> root
    out.activeView = session->attr("activeView") == "1" ? 1 : 0;
    bool anyView = false;
    static const char* const tags[2] = { "mainView", "subView" };
    for (int v = 0; v < 2; ++v)
        for (const XmlElement& view : session->children)
        {
            if (view.name != tags[v]) continue;
            anyView = true;
            for (const XmlElement& f : view.children)
            {
                if (f.name != "File" || f.attr("filename").empty()) continue;
                NppSessionFile file;
                file.path = f.attr("filename");
                file.caret = number(f.attr("startPos"), 0);
                file.firstLine = number(f.attr("firstVisibleLine"), 0);
                for (const XmlElement& m : f.children)
                    if (m.name == "Mark")
                    {
                        const long long line = number(m.attr("line"), -1);
                        if (line >= 0) file.marks.push_back(line);
                    }
                out.views[v].push_back(file);
            }
            const long long a = number(view.attr("activeIndex"), -1);
            out.active[v] = a >= 0 && a < static_cast<long long>(out.views[v].size()) ? static_cast<int>(a) : -1;
        }
    if (!anyView)
    {
        if (err) *err = "no mainView or subView: not a Notepad++ session";
        return false;
    }
    return true;
}

bool sessionFilesFromNpp(const std::string& xml, std::vector<std::string>& files, std::string* err)
{
    files.clear();
    NppSession s;
    if (!sessionFromNpp(xml, s, err)) return false;
    for (const auto& view : s.views)
        for (const NppSessionFile& f : view) files.push_back(f.path);
    return true;
}

std::string nppSessionXml(const NppSession& s)
{
    std::string x = "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n<NotepadPlus>\n";
    x += "    <Session activeView=\"" + std::to_string(s.activeView ? 1 : 0) + "\">\n";
    static const char* const tags[2] = { "mainView", "subView" };
    for (int v = 0; v < 2; ++v)
    {
        x += std::string("        <") + tags[v] + " activeIndex=\"" + std::to_string(s.active[v] < 0 ? 0 : s.active[v]) + "\"";
        if (s.views[v].empty()) { x += " />\n"; continue; }    // Notepad++ always writes both views
        x += ">\n";
        for (const NppSessionFile& f : s.views[v])
        {
            const std::string pos = std::to_string(f.caret < 0 ? 0 : f.caret);
            x += "            <File firstVisibleLine=\"" + std::to_string(f.firstLine < 0 ? 0 : f.firstLine) + "\" startPos=\"" + pos
               + "\" endPos=\"" + pos + "\" filename=\"" + attrEscape(f.path) + "\"";
            if (f.marks.empty()) { x += " />\n"; continue; }
            x += ">\n";
            for (long long line : f.marks) x += "                <Mark line=\"" + std::to_string(line) + "\" />\n";
            x += "            </File>\n";
        }
        x += std::string("        </") + tags[v] + ">\n";
    }
    x += "    </Session>\n</NotepadPlus>\n";
    return x;
}

std::string nppSessionXml(const std::vector<std::string> views[2], const int active[2], int activeView)
{
    NppSession s;
    for (int v = 0; v < 2; ++v)
    {
        for (const std::string& p : views[v]) { NppSessionFile f; f.path = p; s.views[v].push_back(f); }
        s.active[v] = active[v];
    }
    s.activeView = activeView;
    return nppSessionXml(s);
}

// The shape src/main.cpp's writeSessionFile/readSessionFile use: activeView, then main: and sub:, each
// with its active index and its files - a path, or {path, caret, firstLine, bookmarks}.
bool sessionFromWxnote(const std::string& yaml, NppSession& out)
{
    out = NppSession();
    wxnyaml::Doc doc;
    if (!wxnyaml::parse(yaml, doc, "session")) return false;
    const wxnyaml::Node root = doc.root();
    static const char* const names[2] = { "main", "sub" };
    bool anyView = false;
    for (int v = 0; v < 2; ++v)
    {
        const wxnyaml::Node view = wxnyaml::child(root, names[v]);
        if (!wxnyaml::isMap(view)) continue;
        anyView = true;
        const wxnyaml::Node files = wxnyaml::child(view, "files");
        if (wxnyaml::isSeq(files))
            for (wxnyaml::Node e : files.children())
            {
                NppSessionFile f;
                if (!wxnyaml::getText(e, f.path) && wxnyaml::isMap(e))
                {
                    f.path = wxnyaml::textOr(wxnyaml::child(e, "path"), std::string());
                    f.caret = wxnyaml::integerOr(wxnyaml::child(e, "caret"), 0);
                    f.firstLine = wxnyaml::integerOr(wxnyaml::child(e, "firstLine"), 0);
                    const wxnyaml::Node marks = wxnyaml::child(e, "bookmarks");
                    if (wxnyaml::isSeq(marks))
                        for (wxnyaml::Node m : marks.children())
                        {
                            long long line = 0;
                            if (wxnyaml::getInteger(m, line) && line >= 0) f.marks.push_back(line);
                        }
                }
                if (!f.path.empty()) out.views[v].push_back(f);
            }
        const long long a = wxnyaml::integerOr(wxnyaml::child(view, "active"), -1);
        out.active[v] = a >= 0 && a < static_cast<long long>(out.views[v].size()) ? static_cast<int>(a) : -1;
    }
    out.activeView = wxnyaml::integerOr(wxnyaml::child(root, "activeView"), 0) == 1 ? 1 : 0;
    return anyView;
}

std::string wxnoteSessionYaml(const NppSession& s)
{
    ryml::Tree t;
    wxnyaml::MutNode root = wxnyaml::resetToMap(t);
    wxnyaml::setInteger(wxnyaml::addKey(root, "activeView"), s.activeView ? 1 : 0);
    static const char* const names[2] = { "main", "sub" };
    for (int v = 0; v < 2; ++v)
    {
        wxnyaml::MutNode view = wxnyaml::addMap(root, names[v]);
        wxnyaml::setInteger(wxnyaml::addKey(view, "active"), s.active[v]);
        wxnyaml::MutNode files = wxnyaml::addSeq(view, "files");
        for (const NppSessionFile& f : s.views[v])
        {
            wxnyaml::MutNode e = wxnyaml::addMapItem(files);
            wxnyaml::setOneLine(e);
            wxnyaml::setText(wxnyaml::addKey(e, "path"), f.path);
            if (f.caret > 0) wxnyaml::setInteger(wxnyaml::addKey(e, "caret"), f.caret);
            if (f.firstLine > 0) wxnyaml::setInteger(wxnyaml::addKey(e, "firstLine"), f.firstLine);
            if (!f.marks.empty())
            {
                wxnyaml::MutNode b = wxnyaml::addSeq(e, "bookmarks");
                wxnyaml::setOneLine(b);
                for (long long line : f.marks) wxnyaml::setInteger(wxnyaml::addItem(b), line);
            }
        }
    }
    std::string out;
    if (!wxnyaml::emit(t, out)) return std::string();
    return "# wxNote session, from a Notepad++ session\n" + out;
}

}   // namespace nppcompat
