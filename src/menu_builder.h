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
#include "keymap_store.h"         // seedKeymapDefaults() feeds Tier 0 (MenuItemDef.defaultAccel) into the store

// Items are appended with the BARE translated label (a genuine _() call site, so gettext extraction
// and the existing catalogs are unaffected). The accelerator suffix is NOT appended here: every
// accel-bearing data row carries a symbolicName + a real command id (verified across all
// menu_data_*.h tables), so every such item is store-managed, and buildMenuBar's
// refreshAccelerators() -> rewriteMenuAccelLabels() attaches "\t<accel>" from the KeymapStore on
// BOTH frame paths before the frame is first shown. That rewrite is the single source of truth for
// the displayed accel (defaults, schemes and user overrides alike); re-appending the default here
// too was redundant and would go stale the moment a scheme/override diverges from the default.
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

// Seed the KeymapStore's Tier 0 (wxnote.default) by walking the SAME static MenuItemDef tables the menu
// bar is built from, recording {symbolicName -> (cmdId, defaultAccel)} for every command item. Walking
// the DATA (not the built wxMenuBar) is what lets the store carry the locale-independent defaultAccel and
// the symbolicName, neither of which a built wxMenuItem exposes. The
// runtime-inserted Language submenu, Recent Files, plugin/macro entries and DynamicSlots are excluded:
// they carry no default accelerator (their remapping is a later phase).
inline void seedKeymapDefaultsFromDefs(const MenuItemDef* items, size_t count, KeymapStore& store)
{
    for (size_t i = 0; i < count; ++i)
    {
        const MenuItemDef& d = items[i];
        if (d.kind == MenuItemKind::Submenu)
            seedKeymapDefaultsFromDefs(d.children, d.childCount, store);
        else if ((d.kind == MenuItemKind::Normal || d.kind == MenuItemKind::Check) && d.symbolicName && d.id != 0)
            store.addDefault(wxString::FromAscii(d.symbolicName), d.id,
                             d.defaultAccel ? wxString::FromAscii(d.defaultAccel) : wxString());
    }
}

// SECONDARY Tier-0 default accels - the "bind both" rows of the 6-editor consensus survey, where two
// chords are each shipped by 4-5 of the surveyed editors and users arrive expecting either:
//   * redo:      Ctrl+Y is the 4-vote primary, but Ctrl+Shift+Z is the ONLY redo chord bound in all 6;
//   * close tab: Ctrl+W (TextMate/Sublime/Pulsar primary) and Ctrl+F4 (VSCode primary; Sublime/Pulsar
//                co-default) are both live nearly everywhere.
// MenuItemDef.defaultAccel is deliberately single-accel (it doubles as the menu-label suffix); these ride
// along via KeymapStore::addDefaultSecondary, which mirrors the user layer's bare-bind ADD - the extra
// accel resolves AFTER the primary, so the menu label keeps showing the primary only, and a user
// rebind/unbind (the mapper's unbind-then-bind replace) clears both together. Dispatch is per frame
// path: the borderless frame's accel table installs ALL of a binding's accels (collectFrameAccels),
// while the native menubar derives only ONE accel from the label suffix - rewriteMenuAccelLabels
// (main.cpp) therefore installs EffectiveBinding::secondaryRaws() there via wxMenuItem::AddExtraAccel.
struct SecondaryDefaultDef { const char* symbolicName; const char* accel; };
inline constexpr SecondaryDefaultDef kSecondaryDefaults[] = {
    { "edit.redo",  "Ctrl+Shift+Z" },
    { "file.close", "Ctrl+F4" },
};

inline void seedKeymapDefaults(KeymapStore& store)
{
    static const MenuDef* kSeedMenus[] = {
        &kFileMenu, &kEditMenu, &kSelectionMenu, &kGoMenu, &kViewMenu,
        &kDocumentMenu, &kAutomationMenu, &kExtensionsMenu, &kSettingsMenu, &kWindowMenu, &kHelpMenu,
    };
    for (const MenuDef* def : kSeedMenus)
        seedKeymapDefaultsFromDefs(def->items, def->itemCount, store);
    for (const SecondaryDefaultDef& s : kSecondaryDefaults)
        store.addDefaultSecondary(wxString::FromAscii(s.symbolicName), wxString::FromAscii(s.accel));
}
