// SPDX-License-Identifier: GPL-3.0-or-later
//
// npp-compat - Notepad++'s session file: read one, write one, and turn it into wxNote's and back.
// Copyright 2026 The wxNote Authors. See LICENSE (GPL-3.0-or-later).
//
// Two users: the npp-compat plugin, which opens a Notepad++ session in wxNote, and the npp-bridge,
// which answers Notepad++ plugins' NPPM_*SESSION* messages in Notepad++'s own format (wxNote's own
// sessions are YAML). The bridge saves and loads through the host's session files (nib.session) so the
// caret, the first visible line, the bookmarks and the active tabs come across, and converts with this.
// The header uses only the standard library, so the bridge can include it after <windows.h>.

#pragma once

#include <string>
#include <vector>

namespace nppcompat {

// One file of a session. Notepad++ and wxNote both keep the position and bookmarks of the active file.
struct NppSessionFile
{
    std::string             path;
    long long               caret = 0;        // Notepad++'s startPos
    long long               firstLine = 0;    // firstVisibleLine
    std::vector<long long>  marks;            // bookmarked lines (0-based)
};
struct NppSession
{
    std::vector<NppSessionFile> views[2];     // the main view, then the sub view
    int active[2] = { -1, -1 };               // each view's active file, -1 for none
    int activeView = 0;
};

// A Notepad++ session file read. False (with *err set) if the text is not one.
bool sessionFromNpp(const std::string& xml, NppSession& out, std::string* err = nullptr);
// The files a Notepad++ session lists, main view first.
bool sessionFilesFromNpp(const std::string& xml, std::vector<std::string>& files, std::string* err = nullptr);
// A Notepad++ session file.
std::string nppSessionXml(const NppSession& s);
// ...listing views[0] (the main view) and views[1] (the sub view), with active[v] the index of each view's
// active file and activeView the view that has the focus.
std::string nppSessionXml(const std::vector<std::string> views[2], const int active[2], int activeView);

// wxNote's own session file (YAML, as nib.session reads and writes it) read, and written.
bool sessionFromWxnote(const std::string& yaml, NppSession& out);
std::string wxnoteSessionYaml(const NppSession& s);

}   // namespace nppcompat
