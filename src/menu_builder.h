#pragma once
// Walks the MenuBarDef data tables (see menu_data_*.h) once at startup to build the real wxMenuBar,
// replacing the old ~700-line imperative inline menu construction. Also builds a MenuRegistry mapping each
// menu's stable symbolicName to its wxMenu* / bar position / DynamicSlot insertion point, so the
// handful of main.cpp call sites that need a specific menu at runtime (Recent Files, the
// Localization picker, Nib plugin commands, saved Macro entries) no longer depend on translated
// menu-label text via wxMenuBar::FindMenu().
#include "menu_model.h"
#include "menu_data_file.h"
#include "menu_data_edit.h"
#include "menu_data_selection.h"
#include "menu_data_search.h"     // kGoMenu (renamed from Search in the Phase B reshape; content unchanged)
#include "menu_data_view.h"
#include "menu_data_document.h"   // Document: Language (DynamicSlot, see below) + Encoding submenu
#include "menu_data_language.h"   // buildLanguageMenu() - the one hand-written generator (see its own header comment)
#include "menu_data_automation.h" // Automation: Macro + Run + Tools' hash submenus
#include "menu_data_plugins.h"    // kExtensionsMenu (renamed from Plugins in the Phase B reshape; content unchanged)
#include "menu_data_settings.h"   // Settings: config items + Localization (DynamicSlot, see main.cpp) - split back out of Window
#include "menu_data_window.h"     // Window: window management only (Sort By / Windows / Recent Window)
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

// Builds the whole menu bar and populates `reg`. Eleven top-level menus: File, Edit, Selection, Go,
// View, Document, Automation, Extensions, Settings, Window, Help. (The Phase B reshape had merged
// Settings into Window for a round 10; it was split back out after user feedback that Preferences
// under "Window" was a weird home.) Every command id and every item's internal wording/nesting is
// unchanged; only which top-level menu owns what changed.
inline void buildWxnMainMenu(wxMenuBar* mb, MenuRegistry& reg)
{
    struct Entry { const MenuDef* def; };
    static const Entry kMenus[] = {
        { &kFileMenu }, { &kEditMenu }, { &kSelectionMenu }, { &kGoMenu }, { &kViewMenu },
        { &kDocumentMenu }, { &kAutomationMenu }, { &kExtensionsMenu }, { &kSettingsMenu }, { &kWindowMenu }, { &kHelpMenu },
    };
    int barPos = 0;
    for (const Entry& e : kMenus)
    {
        wxMenu* menu = buildMenuFromDefs(e.def->items, e.def->itemCount, reg);
        mb->Append(menu, e.def->label());
        reg.registerMenu(e.def->symbolicName, menu, barPos++);
    }

    // Language: a hand-written generator (not a static MenuItemDef table) - see menu_data_language.h's
    // own header comment for why. Insert it as a submenu at Document's "slot.language" DynamicSlot;
    // buildLanguageMenu() self-registers "menu.language" as it returns.
    {
        auto [docMenu, langAt] = reg.slot("slot.language");
        if (docMenu)
        {
            wxMenu* lang = buildLanguageMenu(reg);
            docMenu->Insert(langAt, wxID_ANY, Label::MenuLanguage(), lang);
        }
    }
}
