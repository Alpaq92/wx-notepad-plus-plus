// SPDX-License-Identifier: Apache-2.0
//
// wxNote - Command Palette: fuzzy-find any menu command by name and run it.
// Copyright 2026 The wxNote Authors.
//
// This file is only the HARVEST - walking the live wxMenuBar into FilterRows. The dialog itself is
// the shared src/filter_list_dialog.h, which quick-open also uses.
//
// Harvesting the live menu bar rather than the static menu_data_*.h tables is deliberate: the live bar
// already has the translated labels, the current enabled/checked state, the accelerator text, and the
// runtime-generated entries (languages, macros, plugin commands) that no static table contains.

#pragma once

#include "filter_list_dialog.h"
#include "keymap_store.h"

#include <wx/menu.h>
#include <wx/menuitem.h>

#include <unordered_set>
#include <vector>

// Everything a palette row needs from one wxMenuItem, plus the menu path it was found under.
inline void wxnHarvestMenu(wxMenu* menu, const wxString& path, const KeymapStore* keymap,
                           int mruBase, int mruCount,
                           std::unordered_set<int>& seen, std::vector<FilterRow>& out)
{
    if (!menu) return;
    for (wxMenuItem* it : menu->GetMenuItems())
    {
        if (!it || it->IsSeparator()) continue;

        if (it->IsSubMenu())
        {
            // Single-character submenu titles are the Language menu's A/B/C buckets - real grouping for
            // a menu, pure noise in a flat searchable list, so they are flattened away rather than
            // becoming "Document > Language > K > Kotlin".
            const wxString label = it->GetItemLabelText();
            const wxString child = (label.length() > 1)
                                 ? (path.empty() ? label : path + " › " + label)
                                 : path;
            wxnHarvestMenu(it->GetSubMenu(), child, keymap, mruBase, mruCount, seen, out);
            continue;
        }

        const int id = it->GetId();
        if (id <= 0 || id == wxID_ANY) continue;
        // The MRU list: paths, not commands. The range comes from the live wxFileHistory rather than
        // wxID_FILE1..wxID_FILE9, which is only NINE ids - the history holds ten by default and up to
        // fifty, so the fixed constants would let the tenth recent file through as a bogus command row
        // whose label is a raw file path and which does nothing when picked.
        if (mruCount > 0 && id >= mruBase && id < mruBase + mruCount) continue;
        if (!seen.insert(id).second) continue;                // the same command can appear in two menus

        FilterRow r;
        r.primary   = it->GetItemLabelText();
        r.secondary = path;
        r.enabled   = it->IsEnabled();
        r.userId    = id;
        if (r.primary.empty()) { seen.erase(id); continue; }

        // Prefer the keymap's effective binding (which reflects the user's remappings) over the
        // accelerator baked into the menu label.
        wxString accel;
        if (keymap)
            if (const EffectiveBinding* b = keymap->effectiveByCmd(id))
                if (!b->accels.empty())
                    accel = keySpell::displayAccel(b->accels.front().raw);
        if (accel.empty())
        {
            const wxString full = it->GetItemLabel();
            const int tab = full.Find('\t');
            if (tab != wxNOT_FOUND) accel = keySpell::displayAccel(full.Mid(tab + 1));
        }
        r.trailing = accel;

        prepareFilterRow(r, /*pathMode=*/false);
        out.push_back(std::move(r));
    }
}

// Walks every top-level menu. Returns rows ready to hand to FilterListDialog.
// [mruBase, mruBase + mruCount) is the caller's live Recent Files id range, excluded from the result.
inline std::vector<FilterRow> wxnHarvestCommands(wxMenuBar* mb, const KeymapStore* keymap,
                                                 int mruBase = wxID_FILE1, int mruCount = 9)
{
    std::vector<FilterRow> out;
    if (!mb) return out;
    std::unordered_set<int> seen;
    out.reserve(512);
    for (size_t i = 0; i < mb->GetMenuCount(); ++i)
        wxnHarvestMenu(mb->GetMenu(i), mb->GetMenuLabelText(i), keymap, mruBase, mruCount, seen, out);
    return out;
}
