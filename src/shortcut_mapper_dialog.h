#pragma once
// =====================================================================
// shortcut_mapper_dialog.h - the Shortcut Mapper (implements the
// reserved command kCmdSettingShortcutMapper == 48009). One searchable grid over every menu command's
// effective binding, a scheme picker, a key-capture sub-dialog, and a live conflict engine - a pure VIEW
// over the frame's KeymapStore: every edit calls store.rebind()/unbind()/resetToDefault() (which write to
// the floating USER layer, VS Code mode), then store.save() (writes shortcuts.json immediately,
// never on exit), then an apply callback the frame wires to refreshAccelerators().
//
// Layout and theming follow onPreferences()/themeDialog() in main.cpp. Accelerators persist ONLY as
// wxAcceleratorEntry::ToRawString() (invariant English tokens), never the localized ToString() and never
// the menu label text - so a rebind survives a UI-language switch. Command NAMES shown in the
// grid come from the live wxMenuBar (GetItemLabelText()), so they follow the UI language for free.
//
// The grid is a wxDataViewCtrl backed by a wxDataViewListStore subclass (MapperModel) whose GetAttrByRow
// paints conflicted rows red - drawing conflicted rows red needs a per-row attribute the plain
// wxDataViewListCtrl doesn't expose. Only static menu commands are listed in this phase; the curated
// Scintilla editor rows are a later phase, but the conflict engine ALREADY consults the Scintilla default
// mirror so a menu binding that shadows an editor default (Ctrl+Shift+T over SCI_LINECOPY) is flagged now.
// =====================================================================
#include "keymap_store.h"
#include "conflict_engine.h"
#include "shortcut_labels.h"    // editorCommandDisplayName - names the curated editor-command rows
#include <wx/dialog.h>
#include <wx/dataview.h>
#include <wx/choice.h>
#include <wx/srchctrl.h>
#include <wx/tglbtn.h>
#include <wx/checkbox.h>
#include <wx/statline.h>
#include <wx/stattext.h>
#include <wx/sizer.h>
#include <wx/button.h>
#include <wx/textctrl.h>
#include <wx/arrstr.h>     // wxSplit (filter tokenization)
#include <functional>
#include <vector>
#include <algorithm>
#ifdef __WXMSW__
    #include <wx/headerctrl.h>   // wxHeaderCtrl - the generic wxDataViewCtrl's header (GenericGetHeader)
    #include <windows.h>
    #include <uxtheme.h>         // SetWindowTheme - dark-theme the grid's native header band (see darkenGridHeaderMSW)
#endif

// A wxDataViewListStore that can paint individual rows: the mapper marks conflicted rows so
// GetAttrByRow tints them red. Rows are appended in filtered order; m_conflict runs parallel to them.
class MapperModel : public wxDataViewListStore
{
public:
    std::vector<unsigned char> m_conflict;   // per row: 1 => warn-level conflict (red), 0 => normal
    void clearRows()
    {
        DeleteAllItems();
        m_conflict.clear();
    }
    void addRow(const wxString& name, const wxString& shortcut, const wxString& scope,
                const wxString& source, bool conflict)
    {
        // Push the conflict flag BEFORE AppendItem: AppendItem fires a RowAppended notification that can
        // consult GetAttrByRow synchronously, so the parallel flag must already be in place or the row's
        // first paint would miss its red tint.
        m_conflict.push_back(conflict ? 1 : 0);
        wxVector<wxVariant> v;
        v.push_back(wxVariant(name));
        v.push_back(wxVariant(shortcut));
        v.push_back(wxVariant(scope));
        v.push_back(wxVariant(source));
        AppendItem(v);
    }
    bool GetAttrByRow(unsigned row, unsigned /*col*/, wxDataViewItemAttr& attr) const override
    {
        if (row < m_conflict.size() && m_conflict[row]) { attr.SetColour(wxColour(0xE0, 0x40, 0x40)); return true; }
        return false;
    }
};

class ShortcutMapperDialog : public wxDialog
{
public:
    ShortcutMapperDialog(wxWindow* parent, KeymapStore& store, wxMenuBar* mb, bool dark,
                         std::function<void(wxWindow*)> themeFn, std::function<void()> onApply)
        : wxDialog(parent, wxID_ANY, _("Shortcut Mapper"), wxDefaultPosition, wxSize(720, 560),
                   wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
        , m_store(store), m_mb(mb), m_dark(dark), m_theme(std::move(themeFn)), m_apply(std::move(onApply))
    {
        buildRows();
        buildUi();
        rebuildEngine();
        refillGrid();
        updateButtons();
        if (m_theme) m_theme(this);
#ifdef __WXMSW__
        darkenGridHeaderMSW();   // AFTER m_theme(): it must override themeChildProc's blanket header theme
#endif
    }

private:
    // ---- one grid-eligible command --------------------------------------------------------------
    // Either a static menu command (editor==false: cmdId + symbolicName) or a curated Scintilla editor
    // command (editor==true: sym holds its stable "editor.*" name, sciCmd its SCI_* id, cmdId==0).
    // `name` caches the display label, resolved ONCE in buildRows(): it is invariant for the dialog's
    // lifetime (the UI language can't switch under a modal dialog, and GetItemLabelText() strips the
    // accel suffix so a rebind never changes it), while re-resolving it per row per refill cost a
    // recursive wxMenuBar::FindItem walk on every filter keystroke. Appended last so the positional
    // initializers below stay valid.
    struct Row { int cmdId = 0; wxString sym; bool editor = false; int sciCmd = 0; wxString name; };

    // Menu name for a command id via the live menubar (follows the UI language). Falls back to the
    // symbolicName if the item isn't on the bar (shouldn't happen for seeded commands).
    wxString cmdName(int cmdId, const wxString& sym) const
    {
        if (m_mb) if (wxMenuItem* it = m_mb->FindItem(cmdId)) { const wxString t = it->GetItemLabelText(); if (!t.empty()) return t; }
        return sym;
    }
    // The display name for any row (menu label, or the translated editor-command label).
    wxString rowName(const Row& r) const
    {
        return r.editor ? editorCommandDisplayName(r.sym) : cmdName(r.cmdId, r.sym);
    }

    static wxString scopeName(KeyScope s)
    {
        switch (s)
        {
            case KeyScope::Editor:   return _("Editor");
            case KeyScope::Terminal: return _("Terminal");
            default:                 return _("Global");
        }
    }
    static wxString sourceName(BindingSource s)
    {
        switch (s)
        {
            case BindingSource::Scheme: return _("Scheme");
            case BindingSource::User:   return _("User");
            default:                    return _("Default");
        }
    }
    // Localized DISPLAY of one stored raw accel ("Ctrl+Alt+S" tokens) - ToString() is locale-aware; a chord
    // or unparsable string shows verbatim; empty shows empty.
    static wxString accelDisplayRaw(const wxString& raw)
    {
        if (raw.empty()) return wxString();
        if (keySpell::isChord(raw)) return raw;
        wxAcceleratorEntry e;
        if (e.FromString(raw)) { const wxString s = e.ToString(); if (!s.empty()) return s; }
        return raw;
    }
    // Localized DISPLAY of a menu command's effective accels. Multiple accels join with ", ".
    static wxString accelDisplay(const EffectiveBinding& b)
    {
        wxString out;
        for (const EffectiveAccel& a : b.accels)
        {
            if (a.raw.empty()) continue;
            if (!out.empty()) out << ", ";
            out << accelDisplayRaw(a.raw);
        }
        return out;
    }

    void buildRows()
    {
        m_rows.clear();
        for (const EffectiveBinding* b : m_store.all())
        {
            if (!b || b->cmdId == 0) continue;
            // Only commands that actually sit on the menu bar are user-facing rows (dynamic ranges - Recent
            // Files, plugin/macro/language entries - are excluded in this phase, exactly as the accel table
            // and the grid are).
            if (m_mb && !m_mb->FindItem(b->cmdId)) continue;
            m_rows.push_back({ b->cmdId, b->symbolicName, false, 0, cmdName(b->cmdId, b->symbolicName) });
        }
        // The curated Scintilla editor commands - Scope = Editor, remapped via CmdKeyAssign
        // rather than the accel table. Appended after the menu rows; the Scope column separates them.
        for (const EditorEffective& e : m_store.editorAll())
            m_rows.push_back({ 0, e.name, true, e.sciCmd, editorCommandDisplayName(e.name) });
    }

    void buildUi()
    {
        auto* panel = this;
        auto* top = new wxBoxSizer(wxVERTICAL);

        // --- scheme picker row ---
        auto* schemeRow = new wxBoxSizer(wxHORIZONTAL);
        schemeRow->Add(new wxStaticText(panel, wxID_ANY, _("Keymap scheme:")), 0, wxALIGN_CENTRE_VERTICAL | wxRIGHT, 8);
        m_scheme = new wxChoice(panel, wxID_ANY);
        fillSchemeChoice();
        schemeRow->Add(m_scheme, 0, wxALIGN_CENTRE_VERTICAL);
        // Read-only store (shortcuts.json written by a NEWER wxNote - save() refuses to clobber it):
        // freeze the mutating UI and say so, instead of letting edits apply live all session and then
        // silently vanish on restart. "Read-Only" is the status-bar string, already in every catalog.
        if (m_store.isReadOnly())
        {
            m_scheme->Disable();
            auto* roNote = new wxStaticText(panel, wxID_ANY, _("Read-Only"));
            roNote->SetForegroundColour(wxColour(0xC0, 0x80, 0x20));
            schemeRow->Add(roNote, 0, wxALIGN_CENTRE_VERTICAL | wxLEFT, 12);
        }
        schemeRow->AddStretchSpacer();
        top->Add(schemeRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);

        // --- filter + record-keys row ---
        auto* filterRow = new wxBoxSizer(wxHORIZONTAL);
        m_filter = new wxSearchCtrl(panel, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
        m_filter->SetDescriptiveText(_("Filter by name or shortcut"));
        m_filter->ShowCancelButton(true);
        filterRow->Add(m_filter, 1, wxALIGN_CENTRE_VERTICAL | wxRIGHT, 8);
        m_record = new wxToggleButton(panel, wxID_ANY, _("Find by shortcut"));
        m_record->SetToolTip(_("Press a shortcut to list the commands it is bound to"));
        filterRow->Add(m_record, 0, wxALIGN_CENTRE_VERTICAL);
        top->Add(filterRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 8);

        // --- the grid ---
        m_model = new MapperModel();
        m_model->AppendColumn("string");   // 0 Name
        m_model->AppendColumn("string");   // 1 Shortcut
        m_model->AppendColumn("string");   // 2 Scope
        m_model->AppendColumn("string");   // 3 Source
        m_grid = new wxDataViewCtrl(panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxDV_ROW_LINES | wxDV_SINGLE);
        m_grid->AssociateModel(m_model);
        m_model->DecRef();
        m_grid->AppendTextColumn(_("Command"),  0, wxDATAVIEW_CELL_INERT, FromDIP(300), wxALIGN_LEFT, wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE);
        m_grid->AppendTextColumn(_("Shortcut"), 1, wxDATAVIEW_CELL_INERT, FromDIP(170), wxALIGN_LEFT, wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE);
        m_grid->AppendTextColumn(_("Scope"),    2, wxDATAVIEW_CELL_INERT, FromDIP(90),  wxALIGN_LEFT, wxDATAVIEW_COL_RESIZABLE);
        m_grid->AppendTextColumn(_("Source"),   3, wxDATAVIEW_CELL_INERT, FromDIP(90),  wxALIGN_LEFT, wxDATAVIEW_COL_RESIZABLE);
        top->Add(m_grid, 1, wxEXPAND | wxALL, 8);

        // --- conflict info zone + details ---
        auto* infoRow = new wxBoxSizer(wxHORIZONTAL);
        m_info = new wxStaticText(panel, wxID_ANY, _("No conflicts."));
        infoRow->Add(m_info, 1, wxALIGN_CENTRE_VERTICAL);
        // The full-picture report behind the tallies; enabled only while they are non-zero (refillGrid).
        m_btnDetails = new wxButton(panel, wxID_ANY, _("Details..."));
        infoRow->Add(m_btnDetails, 0, wxALIGN_CENTRE_VERTICAL | wxLEFT, 8);
        top->Add(infoRow, 0, wxEXPAND | wxLEFT | wxRIGHT, 12);

        // --- buttons ---
        auto* btn = new wxBoxSizer(wxHORIZONTAL);
        m_btnModify = new wxButton(panel, wxID_ANY, _("Modify..."));
        m_btnClear  = new wxButton(panel, wxID_ANY, _("Clear"));
        m_btnReset  = new wxButton(panel, wxID_ANY, _("Reset to default"));
        btn->Add(m_btnModify, 0, wxRIGHT, 6);
        btn->Add(m_btnClear,  0, wxRIGHT, 6);
        btn->Add(m_btnReset,  0, wxRIGHT, 6);
        btn->AddStretchSpacer();
        btn->Add(new wxButton(panel, wxID_OK, _("Close")), 0);
        top->Add(btn, 0, wxEXPAND | wxALL, 12);

        SetSizer(top);
        Layout();

        // events
        m_scheme->Bind(wxEVT_CHOICE, [this](wxCommandEvent&){ onSchemePicked(); });
        m_filter->Bind(wxEVT_TEXT,   [this](wxCommandEvent&){ refillGrid(); });
        m_filter->Bind(wxEVT_SEARCH, [this](wxCommandEvent&){ refillGrid(); });
        m_filter->Bind(wxEVT_SEARCH_CANCEL, [this](wxCommandEvent&){ m_filter->ChangeValue(wxEmptyString); refillGrid(); });
        m_record->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent&){ onRecordToggle(); });
        m_grid->Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, [this](wxDataViewEvent&){ updateButtons(); updateInfoForSelection(); });
        m_grid->Bind(wxEVT_DATAVIEW_ITEM_ACTIVATED,    [this](wxDataViewEvent&){ onModify(); });
        m_btnModify->Bind(wxEVT_BUTTON, [this](wxCommandEvent&){ onModify(); });
        m_btnClear->Bind(wxEVT_BUTTON,  [this](wxCommandEvent&){ onClear(); });
        m_btnReset->Bind(wxEVT_BUTTON,  [this](wxCommandEvent&){ onReset(); });
        m_btnDetails->Bind(wxEVT_BUTTON, [this](wxCommandEvent&){ onDetails(); });
        // The reverse-lookup capture: a CHAR_HOOK on the dialog, active only while the toggle is armed.
        Bind(wxEVT_CHAR_HOOK, &ShortcutMapperDialog::onCharHook, this);
    }

#ifdef __WXMSW__
    // WHY (dark mode): darken the grid's column-header band. On MSW the wxDataViewCtrl is the GENERIC
    // implementation and its header is a real comctl32 SysHeader32 (wxMSWHeaderCtrl inside the
    // wxHeaderCtrl composite, wx's src/msw/headerctrl.cpp). themeDialog()'s recursive colour pass DOES
    // reach it - wxCompositeWindow forwards SetBackground/ForegroundColour to the native part, which
    // installs a wxMSWHeaderCtrlCustomDraw - but that helper only calls SetTextColor/SetBkColor on the
    // DC at CDDS_ITEMPREPAINT and returns CDRF_DODEFAULT (wx's src/msw/customdraw.cpp). The default
    // painter honours the TEXT colour, while the item BAND is filled by the visual-style theme part
    // (or by COLOR_3DFACE if the theme is stripped; SetBkColor only backfills the text run), so no
    // wx-level colour can darken it. Worse, themeDialog's themeChildProc blanket-applies
    // "DarkMode_Explorer" to every native child and that theme has no dark Header part: the band
    // renders WHITE under light-grey custom-draw text - the washed-out header this fixes.
    //
    // The one lever the native control offers is the theme itself: retheme the header to
    // "DarkMode_ItemsView", the same theme wx's own MSW dark mode picks for headers
    // (wxMSWHeaderCtrl::MSWGetDarkModeSupport). It paints a flat dark band (#191919, filler past the
    // last column and hover states included) and still honours the custom-draw text colour that
    // themeDialog installed - which must stay, because this theme otherwise draws the labels BLACK
    // (wx documents the same in wxMSWHeaderCtrlCustomDraw::UseHeaderThemeColors). Verified headlessly
    // via WM_PRINTCLIENT pixel histograms: DarkMode_Explorer => 0xFFFFFF band, theme stripped =>
    // 0xF0F0F0 classic band, DarkMode_ItemsView => 0x191919 band with 0xDCDCDC text.
    //
    // Limit: the band colour is the theme's own #191919, one step darker than the dialog's #2D2D2D -
    // an exact match would need full owner-draw of the header (losing native hover/sort-arrow
    // rendering); the slightly darker flat band reads as header/body separation. The wxDV_ROW_LINES
    // alternate tint is unaffected: it derives from the grid's own dark background, not the header.
    void darkenGridHeaderMSW()
    {
        if (!m_dark || !m_grid) return;
        wxHeaderCtrl* hdr = m_grid->GenericGetHeader();   // null with wxDV_NO_HEADER
        if (!hdr) return;
        // The composite wxHeaderCtrl is a plain container; the themable control is its SysHeader32 child.
        HWND native = ::FindWindowExW(static_cast<HWND>(hdr->GetHandle()), nullptr, L"SysHeader32", nullptr);
        if (!native) return;
        ::SetWindowTheme(native, L"DarkMode_ItemsView", nullptr);
        hdr->Refresh();
    }
#endif

    void fillSchemeChoice()
    {
        m_scheme->Clear();
        m_schemeIds.clear();
        for (const KeymapScheme* s : m_store.schemes())
        {
            m_scheme->Append(s->name);
            m_schemeIds.push_back(s->id);
        }
        // Select the active scheme (default to "wxnote.default" position if present).
        const wxString active = m_store.activeScheme();
        for (size_t i = 0; i < m_schemeIds.size(); ++i)
            if (m_schemeIds[i] == active) { m_scheme->SetSelection((int)i); break; }
    }

    void onSchemePicked()
    {
        if (m_store.isReadOnly()) { fillSchemeChoice(); return; }   // re-sync the (disabled) picker; no edit
        const int sel = m_scheme->GetSelection();
        if (sel < 0 || sel >= (int)m_schemeIds.size()) return;
        m_store.setActiveScheme(m_schemeIds[(size_t)sel]);
        m_store.save();
        if (m_apply) m_apply();
        rebuildEngine();
        refillGrid();
        updateButtons();
    }

    // ---- the conflict engine + grid fill ----------------------------------------------------------
    void rebuildEngine()
    {
        m_engine.rebuild(m_store, [this](int cmdId){ return menuLabelText(cmdId); });
        // The engine only rebuilds when the underlying data changed (ctor, scheme switch, every commit),
        // so this is the one place the idle line's global conflict tallies can go stale: mark them dirty
        // and let the NEXT refillGrid() pass re-aggregate them (see the counting logic there).
        m_summaryDirty = true;
    }
    wxString menuLabelText(int cmdId) const
    {
        if (m_mb) if (wxMenuItem* it = m_mb->FindItem(cmdId)) return it->GetItemLabelText();
        return wxString();
    }

    // Rebuild the visible rows from m_rows, honoring the filter (and the reverse-lookup key set if armed).
    void refillGrid()
    {
        m_model->clearRows();
        m_visible.clear();

        const wxString filter = m_filter ? m_filter->GetValue().Lower() : wxString();
        wxArrayString terms = wxSplit(filter, ' ');

        // Global-summary aggregation, folded into this same pass (no second full conflict scan): after a
        // data change (rebuildEngine marks the counts dirty) classify EVERY row - filtered-out and
        // reverse-lookup-hidden ones included - so the idle info line's tallies describe the whole keymap,
        // never just the visible slice. Pure filter keystrokes leave the flag clean, reuse the cached
        // counts, and keep the classify-after-filter optimization below.
        const bool counting = m_summaryDirty;
        int hardCount = 0, warnCount = 0;

        for (const Row& r : m_rows)
        {
            // When counting, classify BEFORE any skip (missing store entry / reverse lookup / filter):
            // the summary is global truth, and a clean row costs only a reverse-index miss.
            ConflictClass cls = ConflictClass::None;
            if (counting)
            {
                cls = r.editor ? m_engine.forEditor(r.sym).cls : m_engine.forCommand(r.cmdId).cls;
                if      (cls == ConflictClass::Hard)           ++hardCount;
                else if (cls == ConflictClass::NonEquivShadow) ++warnCount;
            }

            wxString shortcut, scopeStr, srcStr;

            if (r.editor)
            {
                // Editor rows are excluded from the "Find by shortcut" reverse lookup (that lists menu
                // commands by id; editor commands have no menu id).
                if (m_reverseActive) continue;
                const EditorEffective* e = m_store.editorEffective(r.sym);
                if (!e) continue;
                shortcut = accelDisplayRaw(e->effectiveRaw);
                scopeStr = scopeName(KeyScope::Editor);
                srcStr   = sourceName(e->overridden ? BindingSource::User : BindingSource::Default);
            }
            else
            {
                const EffectiveBinding* b = m_store.effectiveByCmd(r.cmdId);
                if (!b) continue;

                // reverse-lookup restriction (Find by shortcut)
                if (m_reverseActive && std::find(m_reverseCmds.begin(), m_reverseCmds.end(), r.cmdId) == m_reverseCmds.end())
                    continue;

                shortcut = accelDisplay(*b);
                scopeStr = scopeName(b->accels.empty() ? KeyScope::Global : b->accels.front().scope);
                srcStr   = sourceName(b->source);
            }

            if (!terms.IsEmpty())
            {
                const wxString hay = (r.name + " " + shortcut + " " + scopeStr).Lower();
                bool all = true;
                for (const wxString& t : terms) { if (t.empty()) continue; if (!hay.Contains(t)) { all = false; break; } }
                if (!all) continue;
            }

            // The conflict flag only AFTER the filter (when a counting pass hasn't already classified this
            // row above): it is the expensive per-row lookup, it isn't part of the filter hay, and this
            // loop re-runs on every filter keystroke - classifying rows the filter is about to discard
            // was pure waste.
            const bool warn = counting
                ? (cls == ConflictClass::Hard || cls == ConflictClass::NonEquivShadow)
                : (r.editor ? m_engine.forEditor(r.sym).isWarning()
                            : m_engine.forCommand(r.cmdId).isWarning());

            m_model->addRow(r.name, shortcut, scopeStr, srcStr, warn);
            m_visible.push_back(r);
        }
        if (counting) { m_hardCount = hardCount; m_warnCount = warnCount; m_summaryDirty = false; }
        // The details report only has content when the (whole-keymap) tallies are non-zero; the tallies
        // only move in the counting pass above, but Enable() is idempotent-cheap, so just re-apply here.
        if (m_btnDetails) m_btnDetails->Enable(m_hardCount > 0 || m_warnCount > 0);
        m_grid->Refresh();
        updateInfoForSelection();
    }

    int selectedRow() const
    {
        const wxDataViewItem it = m_grid->GetSelection();
        if (!it.IsOk()) return -1;
        const int row = (int)m_model->GetRow(it);
        return (row >= 0 && row < (int)m_visible.size()) ? row : -1;
    }
    const Row* selectedCmd() const { const int r = selectedRow(); return r < 0 ? nullptr : &m_visible[(size_t)r]; }

    void updateButtons()
    {
        // Selection-gated as before, and permanently off for a read-only store (the "Find by shortcut"
        // reverse lookup stays usable - it never writes).
        const bool has = selectedCmd() != nullptr && !m_store.isReadOnly();
        m_btnModify->Enable(has);
        m_btnClear->Enable(has);
        m_btnReset->Enable(has);
    }

    void updateInfoForSelection()
    {
        const Row* r = selectedCmd();
        if (!r) { showSummary(); return; }
        setInfo(r->editor ? m_engine.forEditor(r->sym) : m_engine.forCommand(r->cmdId));
    }

    // The idle (no selection) info line: a truthful GLOBAL summary from the tallies the last counting
    // refill aggregated - "No conflicts." may only claim what the whole keymap actually satisfies; a grid
    // with red rows shows their counts instead ("2 conflicts, 1 warnings" hard vs shadow classes), in the
    // dialog's existing severity colours (red once any hard conflict exists, amber for shadow-only).
    void showSummary()
    {
        if (!m_info) return;
        wxString msg; wxColour col = wxNullColour;
        if (m_hardCount == 0 && m_warnCount == 0)
            msg = _("No conflicts.");
        else
        {
            msg = wxString::Format(_("%d conflicts, %d warnings"), m_hardCount, m_warnCount);
            col = m_hardCount > 0 ? wxColour(0xE0, 0x40, 0x40) : wxColour(0xC0, 0x80, 0x20);
        }
        m_info->SetLabel(msg);
        m_info->SetForegroundColour(col == wxNullColour ? (m_dark ? wxColour(220,220,220) : wxColour(0,0,0)) : col);
        m_info->Refresh();
    }

    // Compose the (translated) conflict message from the engine's structured result.
    void setInfo(const ConflictInfo& ci)
    {
        wxString msg; wxColour col = wxNullColour;
        const wxString key = conflictKeys::display(ci.key);
        // The cross-scope suffix: when the other owner lives in a DIFFERENT scope than the selected row,
        // say so - "Ctrl+F6 is also assigned to X" reads very differently when X only fires in the
        // terminal. Skipped for the editor-default shadow message, whose wording already names the scope.
        const wxString scopeSuffix = (ci.otherScope != ci.selfScope)
            ? " " + wxString::Format(_("(scope: %s)"), scopeName(ci.otherScope))
            : wxString();
        switch (ci.cls)
        {
            case ConflictClass::Hard:
                msg = wxString::Format(_("Conflict: %s is also assigned to \"%s\"."), key, ci.otherName) + scopeSuffix;
                col = wxColour(0xE0, 0x40, 0x40);
                break;
            case ConflictClass::NonEquivShadow:
                // otherSciName empty == the OTHER side is a menu command (this row is the shadowed editor
                // command); the editor-default wording would print an empty %s there, so name the menu
                // command instead, with its scope.
                if (!ci.otherSciName.empty())
                    msg = wxString::Format(_("Warning: %s shadows the built-in editor command %s."), key, ci.otherSciName);
                else
                    msg = wxString::Format(_("Warning: %s is also assigned to \"%s\"."), key, ci.otherName) + scopeSuffix;
                col = wxColour(0xC0, 0x80, 0x20);
                break;
            default:
                msg = _("No conflicts.");
                break;
        }
        if (m_info) { m_info->SetLabel(msg); m_info->SetForegroundColour(col == wxNullColour ? (m_dark ? wxColour(220,220,220) : wxColour(0,0,0)) : col); m_info->Refresh(); }
    }

    // ---- conflict details report ------------------------------------------------------------------
    // One owner's report line: who holds the key, in what scope, from what source. The whole sentence is
    // a single msgid per kind so translators control the punctuation and word order.
    wxString ownerLine(const KeyOwner& o) const
    {
        switch (o.kind)
        {
            case OwnerKind::Menu:
                return wxString::Format(_("\"%s\"  (menu command, scope: %s, source: %s)"),
                                        o.name, scopeName(o.scope), sourceName(o.src));
            case OwnerKind::Editor:
                return wxString::Format(_("\"%s\"  (editor command, scope: %s, source: %s)"),
                                        o.name, scopeName(o.scope), sourceName(o.src));
            case OwnerKind::SciDefault:
                return wxString::Format(_("built-in editor command %s  (editor default, scope: %s)"),
                                        o.name, scopeName(o.scope));
            default:   // OwnerKind::Scoped - the hardcoded terminal chrome keys carry no stored name
                return wxString::Format(_("built-in terminal key  (scope: %s)"), scopeName(o.scope));
        }
    }

    // The "Details..." modal: the engine's allIssues() aggregation rendered as one block per conflicted
    // key - hard conflicts first, then shadow warnings - each listing EVERY owner on the key plus one
    // plain-language consequence sentence. A read-only multiline control so the report is selectable and
    // copyable; rebuilt from the live engine on every open, so it always matches the current tallies.
    void onDetails()
    {
        wxString text;
        for (const ConflictIssue& issue : m_engine.allIssues())
        {
            if (!text.empty()) text << "\n";
            text << issue.keyDisplay << "  -  "
                 << (issue.cls == ConflictClass::Hard ? _("Conflict")
                                                      : _("Warning (shadowed editor command)"))
                 << "\n";
            for (const KeyOwner& o : issue.owners)
                text << "    - " << ownerLine(o) << "\n";
            text << "    " << (issue.cls == ConflictClass::Hard
                     ? _("Both bindings are active in the same context; pressing the key runs only one of them.")
                     : _("When the editor has focus, the menu shortcut wins; the editor command is unreachable."))
                 << "\n";
        }
        if (text.empty()) text = _("No conflicts.");   // defensive - the button is disabled at zero tallies

        wxDialog dlg(this, wxID_ANY, _("Conflict Details"), wxDefaultPosition,
                     wxSize(FromDIP(620), FromDIP(420)), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
        auto* top = new wxBoxSizer(wxVERTICAL);
        auto* report = new wxTextCtrl(&dlg, wxID_ANY, text, wxDefaultPosition, wxDefaultSize,
                                      wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP);
        top->Add(report, 1, wxEXPAND | wxALL, 12);
        auto* btn = new wxBoxSizer(wxHORIZONTAL);
        btn->AddStretchSpacer();
        auto* close = new wxButton(&dlg, wxID_OK, _("Close"));
        close->SetDefault();
        btn->Add(close, 0);
        top->Add(btn, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);
        dlg.SetSizer(top);
        if (m_theme) m_theme(&dlg);
        report->SetInsertionPoint(0);   // top of the report, not scrolled to the end
        dlg.ShowModal();
    }

    // ---- edit actions -----------------------------------------------------------------------------
    // Every mutating entry point early-outs on a read-only store (a shortcuts.json a NEWER wxNote wrote,
    // see KeymapStore::save). The buttons and the scheme picker are disabled too (buildUi/updateButtons),
    // but a grid double-click still lands in onModify(), so the guard lives here as well - an edit that
    // applies live and then silently fails to persist would look successful all session and vanish on
    // restart.
    void onModify()
    {
        if (m_store.isReadOnly()) return;
        const Row* r = selectedCmd();
        if (!r) return;
        const Row row = *r;                             // copy: commit()/refill invalidate the pointer
        wxString accelRaw;
        if (!captureKey(row, accelRaw)) return;         // cancelled

        if (row.editor) { onModifyEditor(row, accelRaw); return; }

        if (accelRaw.empty()) return;                   // nothing captured

        // Conflict check on the proposed binding.
        const NormKey nk = conflictKeys::fromAccelString(accelRaw);
        const ConflictInfo ci = m_engine.forProposed(row.cmdId, nk, KeyScope::Global);
        if (ci.cls == ConflictClass::Hard)
        {
            const int choice = promptHardConflict(conflictKeys::display(nk), ci.otherName);
            if (choice == kCancel) return;
            if (choice == kReassign && ci.otherCmdId != 0)
            {
                if (const EffectiveBinding* other = m_store.effectiveByCmd(ci.otherCmdId))
                {
                    // Steal ONLY the colliding key: a keyed unbind leaves any OTHER accels the command
                    // holds intact (a bare unbind here used to wipe them all), and the batched store
                    // call runs the steal + rebind as one edit with a single resolve.
                    m_store.reassignRebind(other->symbolicName, row.sym, accelRaw);
                    commit();
                    return;
                }
            }
        }
        else if (ci.cls == ConflictClass::NonEquivShadow)
        {
            if (!confirmShadow(conflictKeys::display(nk), ci.otherSciName)) return;
        }

        m_store.rebind(row.sym, accelRaw);
        commit();
    }

    // Editor-row Modify: an empty capture clears the binding; otherwise warn on conflict. No steal on
    // this tier - an editor rebind can shadow a menu accel or another editor key; the warning + red row
    // inform the user and keep-both is the sane default - so a hard conflict asks a straight
    // keep-both-or-cancel question (confirmKeepBoth) rather than the menu path's three-way prompt,
    // whose "Reassign" button had nothing to do here and silently behaved as "Keep both".
    void onModifyEditor(const Row& row, const wxString& accelRaw)
    {
        if (accelRaw.empty()) { m_store.setEditorBinding(row.sym, wxString()); commit(); return; }
        const NormKey nk = conflictKeys::fromAccelString(accelRaw);
        const ConflictInfo ci = m_engine.forProposedEditor(row.sym, row.sciCmd, nk);
        if (ci.cls == ConflictClass::Hard)
        {
            if (!confirmKeepBoth(conflictKeys::display(nk), ci.otherName)) return;
        }
        else if (ci.cls == ConflictClass::NonEquivShadow)
        {
            if (!confirmShadow(conflictKeys::display(nk), ci.otherSciName)) return;
        }
        m_store.setEditorBinding(row.sym, accelRaw);
        commit();
    }

    void onClear()
    {
        if (m_store.isReadOnly()) return;
        const Row* r = selectedCmd();
        if (!r) return;
        // Explicit unbind (not reset): a persisted clear, so a future new default for this command can
        // never silently reappear.
        if (r->editor) m_store.setEditorBinding(r->sym, wxString());
        else           m_store.unbind(r->sym);
        commit();
    }

    void onReset()
    {
        if (m_store.isReadOnly()) return;
        const Row* r = selectedCmd();
        if (!r) return;
        if (r->editor) m_store.resetEditorToDefault(r->sym);
        else           m_store.resetToDefault(r->sym);
        commit();
    }

    // Persist + re-apply + re-index + repaint, keeping the current selection's command highlighted.
    void commit()
    {
        const Row* sel = selectedCmd();
        const bool have = sel != nullptr;
        const Row want = have ? *sel : Row{};
        m_store.save();
        if (m_apply) m_apply();
        rebuildEngine();
        refillGrid();
        if (have) reselectRow(want);
        updateButtons();
        // Programmatic Select() (reselectRow) fires no SELECTION_CHANGED, so restore the selection rule
        // by hand: a selected row shows its own message, no selection shows the (just-recounted) summary.
        updateInfoForSelection();
    }
    // Reselect a row by identity (editor rows all share cmdId 0, so match on the editor flag + name).
    void reselectRow(const Row& want)
    {
        for (size_t i = 0; i < m_visible.size(); ++i)
        {
            const Row& v = m_visible[i];
            const bool same = want.editor ? (v.editor && v.sym == want.sym) : (!v.editor && v.cmdId == want.cmdId);
            if (same)
            {
                const wxDataViewItem it = m_model->GetItem((unsigned)i);
                m_grid->Select(it); m_grid->EnsureVisible(it); break;
            }
        }
    }

    // ---- key capture sub-dialog -------------------------------------------------------------------
    // Read-only field capturing the main key via CHAR_HOOK + modifier checkboxes. Returns true on OK with
    // the canonical ToRawString() spelling in outAccelRaw (empty => the user cleared it).
    bool captureKey(const Row& r, wxString& outAccelRaw)
    {
        wxDialog dlg(this, wxID_ANY, _("Assign Shortcut"), wxDefaultPosition, wxDefaultSize);
        auto* top = new wxBoxSizer(wxVERTICAL);

        top->Add(new wxStaticText(&dlg, wxID_ANY, wxString::Format(_("Shortcut for: %s"), rowName(r))),
                 0, wxALL, 12);

        auto* field = new wxTextCtrl(&dlg, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(240), -1), wxTE_READONLY | wxTE_CENTRE);
        top->Add(field, 0, wxEXPAND | wxLEFT | wxRIGHT, 12);

        auto* mods = new wxBoxSizer(wxHORIZONTAL);
        auto* cbCtrl  = new wxCheckBox(&dlg, wxID_ANY, _("Ctrl"));
        auto* cbAlt   = new wxCheckBox(&dlg, wxID_ANY, _("Alt"));
        auto* cbShift = new wxCheckBox(&dlg, wxID_ANY, _("Shift"));
        mods->Add(cbCtrl, 0, wxRIGHT, 10); mods->Add(cbAlt, 0, wxRIGHT, 10); mods->Add(cbShift, 0);
        top->Add(mods, 0, wxALL, 12);
#ifdef __WXMAC__
        // On macOS token `ctrl` maps to Cmd at runtime; this pair lets the user pick physical (Raw)Ctrl
        // instead. Ctrl checkbox above == Cmd here; rawCtrl forces wxACCEL_RAW_CTRL.
        auto* cbRawCtrl = new wxCheckBox(&dlg, wxID_ANY, _("Control (physical)"));
        top->Add(cbRawCtrl, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);
#endif

        // captured main key state
        int   capKey = 0;
        auto recompose = [&]() {
            int flags = wxACCEL_NORMAL;
            if (cbCtrl->GetValue())  flags |= wxACCEL_CTRL;
            if (cbAlt->GetValue())   flags |= wxACCEL_ALT;
            if (cbShift->GetValue()) flags |= wxACCEL_SHIFT;
#ifdef __WXMAC__
            if (cbRawCtrl->GetValue()) flags |= wxACCEL_RAW_CTRL;
#endif
            if (capKey == 0) { field->ChangeValue(wxEmptyString); return; }
            wxAcceleratorEntry e(flags, capKey, 0);
            field->ChangeValue(e.ToString());
        };
        cbCtrl->Bind(wxEVT_CHECKBOX,  [&](wxCommandEvent&){ recompose(); });
        cbAlt->Bind(wxEVT_CHECKBOX,   [&](wxCommandEvent&){ recompose(); });
        cbShift->Bind(wxEVT_CHECKBOX, [&](wxCommandEvent&){ recompose(); });
#ifdef __WXMAC__
        cbRawCtrl->Bind(wxEVT_CHECKBOX, [&](wxCommandEvent&){ recompose(); });
#endif

        // CHAR_HOOK: a non-modifier key becomes the main key; the down-modifiers tick the boxes too, so a
        // user can just PRESS Ctrl+Shift+K and have it captured whole. Esc/Tab/Enter are structural (let
        // them reach the dialog) unless a modifier is held.
        dlg.Bind(wxEVT_CHAR_HOOK, [&](wxKeyEvent& e){
            const int kc = e.GetKeyCode();
            if (kc == WXK_CONTROL || kc == WXK_ALT || kc == WXK_SHIFT || kc == WXK_RAW_CONTROL) { e.Skip(); return; }
            const bool anyMod = e.ControlDown() || e.AltDown() || e.ShiftDown() || e.RawControlDown();
            if ((kc == WXK_ESCAPE || kc == WXK_TAB || kc == WXK_RETURN || kc == WXK_NUMPAD_ENTER) && !anyMod) { e.Skip(); return; }
            capKey = kc;
            cbCtrl->SetValue(e.ControlDown());
            cbAlt->SetValue(e.AltDown());
            cbShift->SetValue(e.ShiftDown());
#ifdef __WXMAC__
            cbRawCtrl->SetValue(e.RawControlDown());
#endif
            recompose();
            // don't Skip: swallow the captured key so it doesn't type into anything / trigger a default btn
        });

        auto* clearBtn = new wxButton(&dlg, wxID_ANY, _("Clear"));
        clearBtn->Bind(wxEVT_BUTTON, [&](wxCommandEvent&){
            capKey = 0; cbCtrl->SetValue(false); cbAlt->SetValue(false); cbShift->SetValue(false);
#ifdef __WXMAC__
            cbRawCtrl->SetValue(false);
#endif
            recompose();
        });
        auto* btn = new wxBoxSizer(wxHORIZONTAL);
        btn->Add(clearBtn, 0, wxRIGHT, 6);
        btn->AddStretchSpacer();
        auto* ok = new wxButton(&dlg, wxID_OK, _("OK")); ok->SetDefault();
        btn->Add(ok, 0, wxRIGHT, 6);
        btn->Add(new wxButton(&dlg, wxID_CANCEL, _("Cancel")), 0);
        top->Add(btn, 0, wxEXPAND | wxALL, 12);

        dlg.SetSizerAndFit(top);
        if (m_theme) m_theme(&dlg);
        if (dlg.ShowModal() != wxID_OK) return false;

        if (capKey == 0) { outAccelRaw.clear(); return true; }   // OK with nothing => treat as no-op
        int flags = wxACCEL_NORMAL;
        if (cbCtrl->GetValue())  flags |= wxACCEL_CTRL;
        if (cbAlt->GetValue())   flags |= wxACCEL_ALT;
        if (cbShift->GetValue()) flags |= wxACCEL_SHIFT;
#ifdef __WXMAC__
        if (cbRawCtrl->GetValue()) flags |= wxACCEL_RAW_CTRL;
#endif
        wxAcceleratorEntry e(flags, capKey, 0);
        outAccelRaw = e.ToRawString();   // canonical, locale-independent
        return true;
    }

    // ---- conflict prompts -------------------------------------------------------------------------
    enum { kReassign = 1, kKeepBoth, kCancel };
    int promptHardConflict(const wxString& keyDisplay, const wxString& otherName)
    {
        wxDialog dlg(this, wxID_ANY, _("Shortcut Conflict"), wxDefaultPosition, wxDefaultSize);
        auto* top = new wxBoxSizer(wxVERTICAL);
        top->Add(new wxStaticText(&dlg, wxID_ANY,
                 wxString::Format(_("%s is already assigned to \"%s\".\nWhat would you like to do?"), keyDisplay, otherName)),
                 0, wxALL, 14);
        auto* btn = new wxBoxSizer(wxHORIZONTAL);
        auto* reassign = new wxButton(&dlg, wxID_ANY, _("Reassign"));
        auto* keep     = new wxButton(&dlg, wxID_ANY, _("Keep both"));
        auto* cancel   = new wxButton(&dlg, wxID_CANCEL, _("Cancel"));
        btn->Add(reassign, 0, wxRIGHT, 6); btn->Add(keep, 0, wxRIGHT, 6); btn->AddStretchSpacer(); btn->Add(cancel, 0);
        top->Add(btn, 0, wxEXPAND | wxALL, 14);
        int result = kCancel;
        reassign->Bind(wxEVT_BUTTON, [&](wxCommandEvent&){ result = kReassign; dlg.EndModal(wxID_OK); });
        keep->Bind(wxEVT_BUTTON,     [&](wxCommandEvent&){ result = kKeepBoth; dlg.EndModal(wxID_OK); });
        dlg.SetSizerAndFit(top);
        if (m_theme) m_theme(&dlg);
        dlg.ShowModal();
        return result;
    }
    // Two-button confirm for a HARD conflict on the EDITOR tier, where keep-both is the only offered
    // outcome (there is no steal on that tier - see onModifyEditor). Every string here is shared with
    // promptHardConflict, so the catalogs need nothing new.
    bool confirmKeepBoth(const wxString& keyDisplay, const wxString& otherName)
    {
        wxDialog dlg(this, wxID_ANY, _("Shortcut Conflict"), wxDefaultPosition, wxDefaultSize);
        auto* top = new wxBoxSizer(wxVERTICAL);
        top->Add(new wxStaticText(&dlg, wxID_ANY,
                 wxString::Format(_("%s is already assigned to \"%s\".\nWhat would you like to do?"), keyDisplay, otherName)),
                 0, wxALL, 14);
        auto* btn = new wxBoxSizer(wxHORIZONTAL);
        btn->AddStretchSpacer();
        auto* keep = new wxButton(&dlg, wxID_OK, _("Keep both")); keep->SetDefault();
        btn->Add(keep, 0, wxRIGHT, 6); btn->Add(new wxButton(&dlg, wxID_CANCEL, _("Cancel")), 0);
        top->Add(btn, 0, wxEXPAND | wxALL, 14);
        dlg.SetSizerAndFit(top);
        if (m_theme) m_theme(&dlg);
        return dlg.ShowModal() == wxID_OK;
    }
    bool confirmShadow(const wxString& keyDisplay, const wxString& sciName)
    {
        wxDialog dlg(this, wxID_ANY, _("Shortcut Warning"), wxDefaultPosition, wxDefaultSize);
        auto* top = new wxBoxSizer(wxVERTICAL);
        top->Add(new wxStaticText(&dlg, wxID_ANY,
                 wxString::Format(_("%s shadows the built-in editor command %s.\nAssign it anyway?"), keyDisplay, sciName)),
                 0, wxALL, 14);
        auto* btn = new wxBoxSizer(wxHORIZONTAL);
        btn->AddStretchSpacer();
        auto* ok = new wxButton(&dlg, wxID_OK, _("Assign")); ok->SetDefault();
        btn->Add(ok, 0, wxRIGHT, 6); btn->Add(new wxButton(&dlg, wxID_CANCEL, _("Cancel")), 0);
        top->Add(btn, 0, wxEXPAND | wxALL, 14);
        dlg.SetSizerAndFit(top);
        if (m_theme) m_theme(&dlg);
        return dlg.ShowModal() == wxID_OK;
    }

    // ---- reverse lookup ("Find by shortcut") ------------------------------------------------------
    void onRecordToggle()
    {
        m_reverseArmed = m_record->GetValue();
        if (m_reverseArmed)
        {
            m_info->SetLabel(_("Press a shortcut..."));
            m_record->SetFocus();   // so the CHAR_HOOK sees the keys (the grid would eat arrows otherwise)
        }
        else
        {
            m_reverseActive = false;
            m_reverseCmds.clear();
            refillGrid();
            updateInfoForSelection();
        }
    }
    void onCharHook(wxKeyEvent& e)
    {
        if (!m_reverseArmed) { e.Skip(); return; }
        const int kc = e.GetKeyCode();
        if (kc == WXK_CONTROL || kc == WXK_ALT || kc == WXK_SHIFT || kc == WXK_RAW_CONTROL) { e.Skip(); return; }
        if (kc == WXK_ESCAPE) { m_record->SetValue(false); onRecordToggle(); return; }
        int flags = wxACCEL_NORMAL;
        if (e.ControlDown())    flags |= wxACCEL_CTRL;
        if (e.AltDown())        flags |= wxACCEL_ALT;
        if (e.ShiftDown())      flags |= wxACCEL_SHIFT;
        if (e.RawControlDown()) flags |= wxACCEL_RAW_CTRL;
        const NormKey nk = conflictKeys::fromWx(flags, kc);
        m_reverseCmds = m_engine.menuCommandsBoundTo(nk);
        m_reverseActive = true;
        m_record->SetValue(false);
        m_reverseArmed = false;
        refillGrid();
        wxAcceleratorEntry ae(flags, kc, 0);
        m_info->SetLabel(wxString::Format(_("Commands bound to %s: %d"), ae.ToString(), (int)m_reverseCmds.size()));
        // swallow the key
    }

    // ---- state ------------------------------------------------------------------------------------
    KeymapStore&                 m_store;
    wxMenuBar*                   m_mb = nullptr;
    bool                         m_dark = false;
    std::function<void(wxWindow*)> m_theme;
    std::function<void()>        m_apply;

    ConflictEngine               m_engine;
    std::vector<Row>             m_rows;        // all grid-eligible commands, menu order
    std::vector<Row>             m_visible;     // currently displayed (filtered) rows, parallel to m_model
    std::vector<wxString>        m_schemeIds;   // parallel to m_scheme entries

    wxChoice*        m_scheme = nullptr;
    wxSearchCtrl*    m_filter = nullptr;
    wxToggleButton*  m_record = nullptr;
    wxDataViewCtrl*  m_grid   = nullptr;
    MapperModel*     m_model  = nullptr;
    wxStaticText*    m_info   = nullptr;
    wxButton*        m_btnDetails = nullptr;
    wxButton*        m_btnModify = nullptr;
    wxButton*        m_btnClear  = nullptr;
    wxButton*        m_btnReset  = nullptr;

    bool             m_reverseArmed  = false;   // toggle is on, waiting for a keystroke
    bool             m_reverseActive = false;   // a reverse-lookup filter is currently applied
    std::vector<int> m_reverseCmds;

    // Global conflict tallies for the idle info line (showSummary). Recounted by the first refillGrid()
    // after each rebuildEngine() - the dirty flag starts true so the ctor's initial fill counts too.
    bool m_summaryDirty = true;
    int  m_hardCount = 0;   // ConflictClass::Hard rows in the WHOLE keymap
    int  m_warnCount = 0;   // ConflictClass::NonEquivShadow rows in the WHOLE keymap
};
