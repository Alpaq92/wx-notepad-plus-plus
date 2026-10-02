#pragma once
// =====================================================================
// The Language menu: SPECIAL-CASED and intentionally NOT a static
// MenuItemDef table like the other menus (see menu_model.h). Most of its
// content - the 112 built-in languages bucketed into single-letter (A, B,
// C...) submenus - is generated at runtime from wxnLangTable. That shape
// doesn't fit a static array, so this stays a small hand-written generator
// function, buildLanguageMenu(), by deliberate design decision.
//
// The languages themselves - wxnLangTable() and its lookups - are in
// lang_table.h, which needs no wx.
// =====================================================================
#include "menu_model.h"
#include "command_ids.h"
#include "lang_table.h"     // the languages themselves (wxnLangTable), without wx
#include <wx/menu.h>
#include <wx/intl.h>
#include <string>

// The only static/fixed labels in this menu - the ~88 per-language names come straight from
// wxnLangTable's own `name` field, which is NOT translated today (language names like "Python",
// "JavaScript" are used as-is) and must stay that way; see buildLanguageMenu() below.
namespace Label
{
    inline const wxString LanguageNone() { return _("None (Normal Text)"); }
    inline const wxString MenuLanguage() { return _("&Language"); }
    inline const wxString LanguageUserDefinedSubmenu() { return _("User Defined Language"); }
    inline const wxString LanguageOpenUdlDir() { return _("Open User Defined Language folder..."); }
    inline const wxString LanguageUserDefined() { return _("User-Defined"); }
}

// ------------------------------------------------------------- Language
// The full built-in language list, bucketed alphabetically into single-letter submenus (A, B, C,
// ...). The shared wxnLangTable is grouped contiguously by first letter (see the top of this
// file), so a new submenu starts whenever the first letter changes.
inline wxMenu* buildLanguageMenu(class MenuRegistry& reg)
{
    auto* lang = new wxMenu;
    lang->Append(kCmdLangText, Label::LanguageNone());
    lang->AppendSeparator();
    {
        size_t ln; const WxnLang* lt = wxnLangTable(ln);
        wxMenu* sub = nullptr; char cur = 0;
        for (size_t i = 0; i < ln; ++i)
        {
            char first = lt[i].name[0];
            if (first >= 'a' && first <= 'z') first = (char)(first - 'a' + 'A');
            if (first != cur) { sub = new wxMenu; lang->AppendSubMenu(sub, wxString(wxUniChar(first))); cur = first; }
            sub->Append(lt[i].id, lt[i].name);
        }
    }
    lang->AppendSeparator();
    {
        auto* sub = new wxMenu;
        sub->Append(kCmdLangOpenudldir, Label::LanguageOpenUdlDir());
        lang->AppendSubMenu(sub, Label::LanguageUserDefinedSubmenu());
    }
    lang->Append(kCmdLangUser, Label::LanguageUserDefined());

    reg.registerMenu("menu.language", lang, /* barPosition filled in by the caller */ -1);
    return lang;
}
