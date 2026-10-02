// SPDX-License-Identifier: GPL-3.0-or-later
//
// npp-compat - a small XML reader for Notepad++'s own files.
// Copyright 2026 The wxNote Authors. See LICENSE (GPL-3.0-or-later).
//
// Notepad++ keeps its themes, sessions, workspaces, context menu and configuration in XML. This reads
// that XML into a tree - elements, attributes, text - and nothing more: no wx, no library. Like the
// shortcuts.xml scanner beside it, it is XXE-safe by construction: a DOCTYPE (with any internal subset)
// is skipped, never interpreted, and only the five predefined entities and numeric character references
// are decoded - any other "&name;" stays literal text. Depth and element count are bounded and the parse
// is iterative: these files can be planted (the importer probes %APPDATA%\Notepad++ on its own), so
// hostile input must fail cleanly and quickly, never recurse the stack away.

#pragma once

#include <string>
#include <utility>
#include <vector>

namespace nppcompat {

struct XmlElement
{
    std::string name;
    std::vector<std::pair<std::string, std::string>> attrs;
    std::string text;                       // the character data directly inside, concatenated
    std::vector<XmlElement> children;

    // An attribute's value, or `def` when the element has none of that name.
    std::string attr(const char* key, const std::string& def = std::string()) const;
    bool hasAttr(const char* key) const;
    // The first child element called `elementName`, or nullptr.
    const XmlElement* child(const char* elementName) const;
};

// Parse a whole document into its root element. False (with *err set) on anything not well-formed
// enough to trust: unbalanced or mismatched tags, nesting deeper than 64 levels, more than two million
// elements, or no root element at all.
bool parseXml(const std::string& text, XmlElement& root, std::string* err = nullptr);

// The text of the first comment ahead of the root element - a theme's credits block - or "".
std::string leadingXmlComment(const std::string& text);

}   // namespace nppcompat
