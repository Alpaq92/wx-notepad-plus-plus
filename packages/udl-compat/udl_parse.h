// SPDX-License-Identifier: GPL-3.0-or-later
//
// udl-compat - parse a Notepad++ userDefineLang.xml into a UdlDef.
// Copyright 2026 The wxNote Authors. See LICENSE (GPL-3.0-or-later).
//
// A small, self-contained reader for the one N++ format this plugin exists to
// interoperate with. It has no wxWidgets / Scintilla dependency (plain std::string
// in, UdlDef out) so the whole file->UdlDef->Scintillua pipeline is unit-testable
// standalone. The packed "Comments" / "Delimiters" encodings are decoded exactly as
// wxNote's own src/udl.h does (index-prefixed slots), so definitions round-trip the
// same way real Notepad++ files do.

#pragma once

#include "udl_scintillua.h"
#include <string>
#include <vector>

namespace udlcompat {

// Parse the first <UserLang> element in a userDefineLang.xml document into `out`.
// Returns true on success; on false, `*err` (if non-null) gets a short reason.
// Tolerates the two real container shapes: a <NotepadPlus> (or <wxNote>) root with
// <UserLang> children, or a bare <UserLang> root.
bool parseUserDefineLangXml(const std::string& xml, UdlDef& out, std::string* err = nullptr);

// Parse EVERY <UserLang> element in the document (Notepad++'s main userDefineLang.xml holds all of
// the user's languages as siblings). Skips malformed/nameless elements; empty result = none found.
std::vector<UdlDef> parseAllUserDefineLangs(const std::string& xml);

// Read an entire file into a string (binary). Returns "" if it cannot be opened.
std::string readFileToString(const std::string& path);

}  // namespace udlcompat
