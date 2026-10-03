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
enum class NppFileKind { Unknown, Theme, Config, Shortcuts, ContextMenu, Session, Workspace, Languages };
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
// The theme file config.xml names as the active one (its stylerTheme path, as written there), or "" for
// Notepad++'s default stylers.xml.
std::string activeThemeFromConfig(const std::string& xml);
// The file name at the end of a path Notepad++ wrote: after its last '\' or '/', on every platform - a
// config.xml copied from Windows names its theme by a Windows path.
std::string nppFileNameOf(const std::string& path);

// ---- contextMenu.xml -> contextmenu.yaml ---------------------------------------------------------
// Notepad++ and wxNote share their command numbers, so numbered items translate one for one, and an item
// named by its menu text is looked up in Notepad++'s English menu names (npp_menu_names.h) as Notepad++
// looks it up. A FolderName run becomes a submenu, ItemNameAs the item's own label, and a plugin's
// command a {plugin, command} item. Only names no Notepad++ menu has are reported in notTranslated.
bool contextMenuFromNpp(const std::string& xml, std::string& yaml, std::vector<std::string>& notTranslated,
                        std::string* err = nullptr);

// ---- langs.xml and the theme's user-defined keywords -> languages.yaml ---------------------------------
// What the user changed in Notepad++'s language definitions, as languages.yaml entries (language_defs.h):
// the extensions and keywords they added, and the comment tokens they changed. The user's langs.xml began
// as a copy of Notepad++'s own langs.model.xml, so the model (`modelXml`) is what tells the user's
// additions from Notepad++'s stock lists; without it ("") langs.xml cannot be told apart and is left out,
// and only the keywords the theme adds come across - `themeXml` (may be "") is the active theme, where
// Notepad++'s Style Configurator keeps its "User-defined keywords". What langs.xml lacks against the model
// is not taken away: it is as likely an update the copy missed, and Notepad++ itself restores it. Notepad++'s keyword classes land in wxNote's lists as its own lexers
// wire them: by number, except the C family (types in list 1, global classes in 3, C++'s doxygen words in
// every C-family language's 2) and the HTML family, whose languages share the markup's and the embedded
// scripts' lists. substyle1-8 are the language's user keyword groups, in order.
struct LanguagesTranslation
{
    std::string yaml;                         // a languages.yaml document; "" when nothing changed
    std::vector<std::string> languages;       // the wxNote languages it has entries for
    std::vector<std::string> notTranslated;   // what had nowhere to go, or was not compared
    bool compared = false;                    // langs.xml was compared with the model
};
bool languagesFromNpp(const std::string& langsXml, const std::string& modelXml, const std::string& themeXml,
                      LanguagesTranslation& out, std::string* err = nullptr);

// ---- a session: npp_session.h (kept apart so the npp-bridge can include it) ---------------------------

// ---- a Project-panel workspace -> a wxNote workspace .yaml ------------------------------------------
// Notepad++ stores a project's files relative to the workspace file: `baseDir` is that file's folder.
bool workspaceFromNpp(const std::string& xml, const std::string& baseDir, std::string& yaml, std::string* err = nullptr);

}   // namespace nppcompat
