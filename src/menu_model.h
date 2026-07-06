#pragma once
// =====================================================================
// The menu bar as DATA rather than as ~700 lines of inline wxMenu/Append/
// AppendSubMenu calls (see the old src/npp_menu.h, since replaced). A
// MenuItemDef tree describes shape (kind, id, label, nesting); menu_builder.h
// walks it once at startup to build the real wxMenuBar. Reordering, renaming,
// or regrouping items is now a matter of moving table rows/pointers around
// in the per-menu menu_data_*.h files, not restructuring nested C++ calls.
//
// Every IDM_* id here is an existing constant from include/npp-compat/
// menuCmdID.h (the plugin ABI - real Notepad++ plugin binaries invoke
// commands by posting these exact numeric ids via NPPM_MENUCOMMAND). This
// file and everything built on it only ever attaches metadata (label,
// position, nesting) to those ids - it never invents new ones.
// =====================================================================
#include <wx/menu.h>
#include <wx/string.h>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <string>

enum class MenuItemKind : uint8_t
{
    Normal,       // plain command item -> Append(id, label())
    Check,        // -> AppendCheckItem(id, label())
    Separator,    // -> AppendSeparator(); id/label/children all ignored
    Submenu,      // -> AppendSubMenu(built-from-children, label())
    DynamicSlot,  // no item rendered here - just records an insertion point
                  // (parent menu + index) in the MenuRegistry for runtime
                  // content (e.g. the Recent Files submenu) to insert at.
};

// A label "getter": a real, literal _("...") call wrapped in a one-line
// function (see menu_labels_*.h). This keeps every visible string a genuine
// compile-time _() call site - just relocated out of the old inline
// Append() call - so gettext-style extraction tooling still finds it, and
// the existing hand-maintained resources/locale/*.po catalogs (keyed by the
// exact English source string, unchanged by this refactor) keep working.
using LabelFn = const wxString (*)();

struct MenuItemDef
{
    MenuItemKind kind = MenuItemKind::Normal;
    int id = 0;                                // IDM_*/wxID_*; unused for Separator/DynamicSlot
    LabelFn label = nullptr;                   // unused for Separator/DynamicSlot
    const char* symbolicName = nullptr;        // stable ascii key; required on Submenu and
                                                // DynamicSlot rows, optional (but recommended)
                                                // elsewhere - never shown to users, never translated
    const MenuItemDef* children = nullptr;     // for Submenu only
    size_t childCount = 0;                     // for Submenu only
    bool initiallyDisabled = false;            // rare: item starts disabled (e.g. the Window menu's
                                                // "Recent Window" placeholder). Appended last so every
                                                // existing 4-field positional initializer stays valid.
};

struct MenuDef
{
    const char* symbolicName;  // e.g. "menu.file" - what FindMenu()-by-label used to do
    LabelFn label;
    const MenuItemDef* items;
    size_t itemCount;
};

struct MenuBarDef
{
    const MenuDef* menus;
    size_t menuCount;
};

// Built once by menu_builder.h's buildNppMainMenu() as it walks a MenuBarDef, then used by the
// handful of call sites in main.cpp that need to find/insert into a specific menu at runtime
// (Recent Files, the Localization picker, Nib plugin commands, saved Macro entries, UDL entries).
// Replaces today's mb->FindMenu(_("Se&ttings"))-style lookups, which silently break the moment a
// maintainer relabels or reshapes the menu they're searching for by translated text.
class MenuRegistry
{
public:
    void registerMenu(const char* symbolicName, wxMenu* menu, int barPosition)
    {
        if (!symbolicName) return;
        m_byName[symbolicName] = menu;
        m_barPosition[symbolicName] = barPosition;
    }
    void recordSlot(const char* symbolicName, wxMenu* parent, int indexWithinParent)
    {
        if (!symbolicName) return;
        m_slotParent[symbolicName] = parent;
        m_slotIndex[symbolicName] = indexWithinParent;
    }
    // The wxMenu* for a top-level menu or a registered Submenu (nullptr if not found).
    wxMenu* find(const char* symbolicName) const
    {
        auto it = m_byName.find(symbolicName);
        return it == m_byName.end() ? nullptr : it->second;
    }
    // A top-level menu's position on the wxMenuBar (-1 if not found/not top-level).
    int barPositionOf(const char* symbolicName) const
    {
        auto it = m_barPosition.find(symbolicName);
        return it == m_barPosition.end() ? -1 : it->second;
    }
    // The (parent menu, insertion index) recorded for a DynamicSlot row. parent is nullptr if
    // the slot was never registered (e.g. this build's data tables don't define it).
    std::pair<wxMenu*, int> slot(const char* symbolicName) const
    {
        auto p = m_slotParent.find(symbolicName);
        auto i = m_slotIndex.find(symbolicName);
        if (p == m_slotParent.end() || i == m_slotIndex.end()) return { nullptr, -1 };
        return { p->second, i->second };
    }
private:
    std::unordered_map<std::string, wxMenu*> m_byName;
    std::unordered_map<std::string, int> m_barPosition;
    std::unordered_map<std::string, wxMenu*> m_slotParent;
    std::unordered_map<std::string, int> m_slotIndex;
};
