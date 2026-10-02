// SPDX-License-Identifier: GPL-3.0-or-later
//
// npp-compat - Notepad++'s files, translated into wxNote's.
// Copyright 2026 The wxNote Authors. See LICENSE (GPL-3.0-or-later).
//
// The one place that knows both sides: Notepad++'s XML (read with npp_xml.h) on one, wxNote's YAML on
// the other - built with the core's own wx-free headers (theme_file.h, settings_schema.h, yaml_io.h,
// Apache-2.0, which GPL code may use), so what this writes is exactly what wxNote reads. No wx, no
// host: plain strings in and out, so every translation is unit-tested (selftest.cpp) and the npp2wxnote
// command-line tool does the same conversions without wxNote running. docs/SETTINGS_DESIGN.md, section
// "Notepad++", lists what each file becomes.

#pragma once

#include "npp_session.h"
#include "theme_file.h"

#include <string>
#include <utility>
#include <vector>

namespace nppcompat {

// What the Notepad++ file is, judged by its content (the root element and what is under it).
enum class NppFileKind { Unknown, Theme, Config, Shortcuts, ContextMenu, Session, Workspace };
NppFileKind detectNppFile(const std::string& xml);
const char* nppFileKindName(NppFileKind k);

// ---- themes: stylers.xml, themes/*.xml -> themes/<name>.yaml -------------------------------------
// The file's credits comment becomes the YAML header. Notepad++-only attributes wxNote never read
// (keywordClass, colorStyle, the per-style keyword text, the model dates) are left behind.
bool themeFromNpp(const std::string& xml, wxntheme::Theme& out, std::string* err = nullptr);

// ---- config.xml -> settings ----------------------------------------------------------------------
// Each setting wxNote has an equivalent for, as (setting ID, value in settings.yaml's spelling), plus a
// line per Notepad++ preference that was found but has no wxNote counterpart.
struct ConfigTranslation
{
    std::vector<std::pair<std::string, std::string>> settings;
    std::vector<std::string> notTranslated;
};
bool settingsFromConfig(const std::string& xml, ConfigTranslation& out, std::string* err = nullptr);

// ---- contextMenu.xml -> contextmenu.yaml ---------------------------------------------------------
// Notepad++ and wxNote share their command numbers, so items translate one for one. An item named only
// by its menu text, a plugin command or a submenu has no number to carry: those are reported instead.
bool contextMenuFromNpp(const std::string& xml, std::string& yaml, std::vector<std::string>& notTranslated,
                        std::string* err = nullptr);

// ---- a session: npp_session.h (kept apart so the npp-bridge can include it) ---------------------------

// ---- a Project-panel workspace -> a wxNote workspace .yaml ------------------------------------------
// Notepad++ stores a project's files relative to the workspace file: `baseDir` is that file's folder.
bool workspaceFromNpp(const std::string& xml, const std::string& baseDir, std::string& yaml, std::string* err = nullptr);

}   // namespace nppcompat
