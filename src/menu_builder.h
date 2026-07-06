#pragma once
// Walks the MenuBarDef data tables (see menu_data_*.h) once at startup to build the real wxMenuBar,
// replacing the old ~700-line imperative src/npp_menu.h. Also builds a MenuRegistry mapping each
// menu's stable symbolicName to its wxMenu* / bar position / DynamicSlot insertion point, so the
// handful of main.cpp call sites that need a specific menu at runtime (Recent Files, the
// Localization picker, Nib plugin commands, saved Macro entries) no longer depend on translated
// menu-label text via wxMenuBar::FindMenu().
#include "menu_model.h"
#include "menu_data_file.h"
#include "menu_data_edit.h"
#include "menu_data_search.h"
#include "menu_data_view.h"
#include "menu_data_encoding.h"
#include "menu_data_language.h"   // buildLanguageMenu() - the one hand-written generator (see its own header comment)
#include "menu_data_settings.h"
#include "menu_data_tools.h"
#include "menu_data_macro.h"
#include "menu_data_run.h"
#include "menu_data_plugins.h"
#include "menu_data_window.h"
#include "menu_data_help.h"

inline wxMenu* buildMenuFromDefs(const MenuItemDef* items, size_t count, MenuRegistry& reg)
{
    auto* menu = new wxMenu;
    for (size_t i = 0; i < count; ++i)
    {
        const MenuItemDef& d = items[i];
        switch (d.kind)
        {
            case MenuItemKind::Normal:
            {
                wxMenuItem* it = menu->Append(d.id, d.label());
                if (d.initiallyDisabled) it->Enable(false);
                break;
            }
            case MenuItemKind::Check:
                menu->AppendCheckItem(d.id, d.label());
                break;
            case MenuItemKind::Separator:
                menu->AppendSeparator();
                break;
            case MenuItemKind::Submenu:
            {
                wxMenu* sub = buildMenuFromDefs(d.children, d.childCount, reg);
                menu->AppendSubMenu(sub, d.label());
                if (d.symbolicName) reg.registerMenu(d.symbolicName, sub, -1);   // submenus have no bar position
                break;
            }
            case MenuItemKind::DynamicSlot:
                reg.recordSlot(d.symbolicName, menu, (int)menu->GetMenuItemCount());
                break;
        }
    }
    return menu;
}

// Builds the whole menu bar and populates `reg`. Menu order matches the original npp_menu.h exactly:
// File, Edit, Search, View, Encoding, Language, Settings, Tools, Macro, Run, Plugins, Window, Help.
inline void buildNppMainMenu(wxMenuBar* mb, MenuRegistry& reg)
{
    struct Entry { const MenuDef* def; };
    static const Entry kStaticMenus[] = {
        { &kFileMenu }, { &kEditMenu }, { &kSearchMenu }, { &kViewMenu }, { &kEncodingMenu },
    };
    int barPos = 0;
    for (const Entry& e : kStaticMenus)
    {
        wxMenu* menu = buildMenuFromDefs(e.def->items, e.def->itemCount, reg);
        mb->Append(menu, e.def->label());
        reg.registerMenu(e.def->symbolicName, menu, barPos++);
    }

    // Language: a hand-written generator (not a static MenuItemDef table) - see menu_data_language.h's
    // own header comment for why. It self-registers "menu.language" with a placeholder bar position;
    // fix that up now that we know the real one.
    {
        wxMenu* lang = buildLanguageMenu(reg);
        mb->Append(lang, Label::MenuLanguage());
        reg.registerMenu("menu.language", lang, barPos++);
    }

    static const Entry kMoreStaticMenus[] = {
        { &kSettingsMenu }, { &kToolsMenu }, { &kMacroMenu }, { &kRunMenu },
        { &kPluginsMenu }, { &kWindowMenu }, { &kHelpMenu },
    };
    for (const Entry& e : kMoreStaticMenus)
    {
        wxMenu* menu = buildMenuFromDefs(e.def->items, e.def->itemCount, reg);
        mb->Append(menu, e.def->label());
        reg.registerMenu(e.def->symbolicName, menu, barPos++);
    }
}
