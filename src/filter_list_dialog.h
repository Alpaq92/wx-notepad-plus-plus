// SPDX-License-Identifier: Apache-2.0
//
// wxNote - the shared "type to filter a list" dialog behind the Command Palette and Quick Open.
// Copyright 2026 The wxNote Authors.
//
// Header-only, matching src/shortcut_mapper_dialog.h. It owns NO knowledge of what it is listing:
// callers hand it rows and read back the chosen index, so the palette and quick-open are each a thin
// harvest function over the same control rather than two similar dialogs that drift apart.
//
// WHY A MODAL wxDialog AND NOT wxPopupTransientWindow. The control must host a text field that takes
// typing. On MSW a transient popup takes NO keyboard focus without wxPU_CONTAINS_CONTROLS -
// SetFocus() there is a documented no-op, and popupcmn.cpp's focus handling is #ifndef __WXMSW__ - so
// a popup-based palette would be silently mouse-only on Windows. This codebase has already paid for
// that lesson once (see the terminal's list popup, which sidesteps it only by owning no children).
// A wxDialog + ShowModal() takes focus natively on all three ports and is what the Shortcut Mapper
// and Preferences already do, including on the borderless frame.

#pragma once

#include "fuzzy_match.h"

#include <wx/dialog.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/vlbox.h>

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

// One entry in the list. `prep` is built ONCE when the caller harvests rows - never per keystroke,
// which is what keeps ~600 commands (or tens of thousands of files) responsive.
struct FilterRow
{
    wxString primary;     // the main text, matched at full weight
    wxString secondary;   // dimmed context: the menu path, or the directory
    wxString trailing;    // right-aligned column: the accelerator, or a hint
    bool     enabled = true;
    int      userId  = 0;   // opaque to this dialog: a command id, an MRU index, ...
    fuzzy::Item prep;       // prepared match target
    size_t   primaryCps = 0;   // code points in `primary` - the prefix of prep.cp that drawRow paints
};

// Build a row's match target. `pathMode` makes the matcher favour the filename over the directory.
inline void prepareFilterRow(FilterRow& r, bool pathMode)
{
    const wxString hay = r.secondary.empty() ? r.primary : (r.primary + " " + r.secondary);
    r.prep = fuzzy::build(hay.utf8_string(), pathMode);
    // The haystack starts with `primary`, so its code points are already decoded in prep.cp. Recording
    // how many of them belong to the primary lets drawRow slice that prefix instead of re-decoding the
    // string on every paint of every visible row.
    r.primaryCps = fuzzy::countCps(r.primary.utf8_string());   // counts lead bytes; no second decode

    // The primary IS the significant span - the file name, or the command name. build() derived it from
    // the last path separator, which for this "primary then secondary" layout lands inside the deepest
    // DIRECTORY, so without this the bonus for "matched in the file name" would be paid to directories.
    fuzzy::setSignificant(r.prep, 0, r.primaryCps);
}

// One stretch of characters drawn in a single style: [begin, end) of the primary text, highlighted or
// not. Splitting the text into runs is what keeps painting to a couple of DrawText calls per row
// instead of one per character.
struct HlRun { size_t begin, end; bool match; };

// Split [0, n) into maximal runs by whether each index appears in `hit`. `hit` MUST be ascending -
// fuzzy::Result::pos is documented to be - which is what lets this walk both sequences once instead of
// searching `hit` for every character.
inline std::vector<HlRun> wxnHighlightRuns(size_t n, const std::vector<int>& hit)
{
    std::vector<HlRun> runs;
    size_t h = 0;
    for (size_t i = 0; i < n; )
    {
        while (h < hit.size() && static_cast<size_t>(hit[h]) < i) ++h;
        const bool m = h < hit.size() && static_cast<size_t>(hit[h]) == i;

        size_t end = i + 1;
        if (m)
            for (size_t k = h + 1; end < n && k < hit.size() && static_cast<size_t>(hit[k]) == end; ++k) ++end;
        else
            while (end < n && !(h < hit.size() && static_cast<size_t>(hit[h]) == end)) ++end;

        runs.push_back({ i, end, m });
        i = end;
    }
    return runs;
}

class FilterListDialog : public wxDialog
{
public:
    // No pathMode parameter: path-aware ranking is entirely a property of each ROW, baked in by
    // prepareFilterRow() -> fuzzy::build(..., isPath) before the rows get here. A dialog-level flag
    // would be a second, redundant place to express the same thing.
    FilterListDialog(wxWindow* parent, const wxString& title, const wxString& placeholder,
                     std::vector<FilterRow> rows, bool dark)
        : wxDialog(parent, wxID_ANY, title, wxDefaultPosition, wxSize(680, 460),
                   wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
        , m_rows(std::move(rows)), m_dark(dark)
    {
        auto* top = new wxBoxSizer(wxVERTICAL);
        m_field = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                                 wxTE_PROCESS_ENTER);
        m_field->SetHint(placeholder);
        top->Add(m_field, 0, wxEXPAND | wxALL, 8);

        m_list = new ResultList(this);
        top->Add(m_list, 1, wxEXPAND | wxLEFT | wxRIGHT, 8);

        m_footer = new wxStaticText(this, wxID_ANY, wxEmptyString);
        top->Add(m_footer, 0, wxEXPAND | wxALL, 8);
        SetSizer(top);

        // Cached once: drawRow runs per visible row per paint, and neither font ever changes for the
        // life of the dialog.
        m_baseFont = GetFont();
        m_boldFont = m_baseFont;
        m_boldFont.SetWeight(wxFONTWEIGHT_BOLD);

        // CHAR_HOOK, not the text control's key event: the field keeps focus the whole time (so typing
        // never has to be re-routed), while Up/Down/Enter/Esc still drive the list.
        Bind(wxEVT_CHAR_HOOK, &FilterListDialog::onKey, this);
        m_field->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { requery(); });
        m_list->Bind(wxEVT_LISTBOX_DCLICK, [this](wxCommandEvent&) { commit(); });

        requery();
        m_field->SetFocus();
        CentreOnParent();
    }

    // An optional SECOND row set, entered by typing a one-character prefix - '@' for "symbols in this
    // file", the convention every editor with this control uses. It lives here rather than in the
    // caller because the switch has to happen per keystroke, mid-query; a caller-side implementation
    // would have to tear the dialog down and rebuild it on the '@'.
    //
    // The rows arrive as a PROVIDER, not a vector, and it is called at most once - the first time the
    // prefix is actually typed. Quick Open's provider runs the whole-buffer symbol scan, which the
    // Function List pane deliberately gates on being visible (see parseFuncList); building it eagerly
    // would make every Quick Open pay for a parse that is thrown away unless the user types '@'.
    void setAltRows(wxUniChar prefix, std::function<std::vector<FilterRow>()> provider)
    {
        m_altPrefix   = prefix;
        m_altProvider = std::move(provider);
        m_altReady    = false;
        requery();
    }

    // Pre-fill the query. Quick Open seeds this from the selection, so selecting a filename in the
    // text and pressing the shortcut lands on it directly. ChangeValue does not fire wxEVT_TEXT, hence
    // the explicit requery.
    void setQuery(const wxString& q)
    {
        m_field->ChangeValue(q);
        m_field->SetInsertionPointEnd();
        requery();
    }

    // What the ranking currently shows, top-first. This is the seam the self-test drives: asserting on
    // the RANKED VIEW is what proves typing actually filters, without simulating OS-level keystrokes.
    size_t visibleCount() const { return m_view.size(); }
    int    visibleId(size_t i) const { return i < m_view.size() ? active()[m_view[i].row].userId : -1; }

    // Index into whichever row vector was active, or -1 when cancelled or nothing matched.
    int chosen() const { return m_chosen; }
    bool chosenIsAlt() const { return m_chosenAlt; }
    const FilterRow* chosenRow() const
    {
        const std::vector<FilterRow>& v = m_chosenAlt ? m_alt : m_rows;
        return (m_chosen >= 0 && m_chosen < static_cast<int>(v.size())) ? &v[static_cast<size_t>(m_chosen)]
                                                                       : nullptr;
    }
    const std::vector<FilterRow>& rows() const { return m_rows; }

private:
    // ---- the owner-drawn result list ---------------------------------------------------------------
    // wxVListBox rather than wxListCtrl: the matched characters are bolded, which is the whole reason
    // fuzzy::Result carries positions, and a virtual list keeps a 50k-file index affordable.
    class ResultList : public wxVListBox
    {
    public:
        explicit ResultList(FilterListDialog* owner)
            : wxVListBox(owner, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE), m_owner(owner)
        {
            SetBackgroundStyle(wxBG_STYLE_PAINT);
        }

    protected:
        void OnDrawItem(wxDC& dc, const wxRect& rect, size_t n) const override { m_owner->drawRow(dc, rect, n); }
        wxCoord OnMeasureItem(size_t) const override { return m_owner->rowHeight(); }
        void OnDrawBackground(wxDC& dc, const wxRect& rect, size_t n) const override
        {
            const bool sel = IsSelected(n);
            dc.SetBrush(wxBrush(m_owner->rowBg(sel)));
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.DrawRectangle(rect);
        }

    private:
        FilterListDialog* m_owner;
    };

    wxCoord rowHeight() const { return FromDIP(38); }
    wxColour rowBg(bool selected) const
    {
        if (selected) return m_dark ? wxColour(0x37, 0x4a, 0x3c) : wxColour(0xd3, 0xf9, 0xd8);
        return m_dark ? wxColour(0x21, 0x25, 0x29) : wxColour(0xff, 0xff, 0xff);
    }
    wxColour fgPrimary()   const { return m_dark ? wxColour(0xe9, 0xec, 0xef) : wxColour(0x21, 0x25, 0x29); }
    wxColour fgSecondary() const { return m_dark ? wxColour(0x86, 0x8e, 0x96) : wxColour(0x86, 0x8e, 0x96); }
    wxColour fgMatch()     const { return m_dark ? wxColour(0x69, 0xdb, 0x7c) : wxColour(0x2f, 0x9e, 0x44); }
    wxColour fgDisabled()  const { return m_dark ? wxColour(0x59, 0x5c, 0x60) : wxColour(0xad, 0xb5, 0xbd); }

    // Draws primary (with matched code points bolded and tinted), then the dimmed secondary beneath,
    // then the trailing column right-aligned.
    void drawRow(wxDC& dc, const wxRect& rect, size_t view) const
    {
        if (view >= m_view.size()) return;
        const FilterRow& r = active()[m_view[view].row];
        const std::vector<int>& hit = m_view[view].hit.pos;

        const wxColour normal = r.enabled ? fgPrimary() : fgDisabled();

        int x = rect.x + FromDIP(8);
        const int yTop = rect.y + FromDIP(4);

        // The match positions index the concatenated "primary secondary" haystack, so only those below
        // the primary's length belong to this run - and prep.cp already holds it decoded.
        //
        // Drawn as RUNS of same-style characters, not one DrawText per character. fuzzy::Result::pos is
        // ascending, so a single index walks it in lockstep: no std::find per character (that was O(L*H)
        // per row per paint), and an unhighlighted row costs one DrawText instead of thirty.
        const std::vector<char32_t>& cps = r.prep.cp;
        const size_t n = std::min(r.primaryCps, cps.size());
        static const std::vector<int> kNoHits;   // a disabled row is drawn flat, with no highlighting
        for (const HlRun& seg : wxnHighlightRuns(n, r.enabled ? hit : kNoHits))
        {
            wxString run;
            run.reserve(seg.end - seg.begin);
            for (size_t j = seg.begin; j < seg.end; ++j) run += wxUniChar(cps[j]);

            dc.SetFont(seg.match ? m_boldFont : m_baseFont);
            dc.SetTextForeground(seg.match ? fgMatch() : normal);
            dc.DrawText(run, x, yTop);
            x += dc.GetTextExtent(run).GetWidth();
        }

        const wxFont& base = m_baseFont;
        if (!r.secondary.empty())
        {
            dc.SetFont(base);
            dc.SetTextForeground(fgSecondary());
            dc.DrawText(r.secondary, rect.x + FromDIP(8), yTop + FromDIP(16));
        }
        if (!r.trailing.empty())
        {
            dc.SetFont(base);
            dc.SetTextForeground(r.enabled ? fgSecondary() : fgDisabled());
            const int w = dc.GetTextExtent(r.trailing).GetWidth();
            dc.DrawText(r.trailing, rect.GetRight() - w - FromDIP(10), yTop);
        }
    }

    // ---- filtering ----------------------------------------------------------------------------------
    const std::vector<FilterRow>& active() const { return m_inAlt ? m_alt : m_rows; }

    void requery()
    {
        wxString q = m_field->GetValue();
        // The prefix selects the alternate set and is NOT itself part of the needle.
        m_inAlt = (m_altPrefix != wxUniChar(0) && !q.empty() && q[0] == m_altPrefix);
        if (m_inAlt)
        {
            q = q.Mid(1);
            if (!m_altReady) { if (m_altProvider) m_alt = m_altProvider(); m_altReady = true; }
        }
        const std::vector<FilterRow>& rows = active();

        const std::vector<char32_t> raw = fuzzy::decodeUtf8(q.utf8_string());
        std::vector<char32_t> fold(raw.size());
        for (size_t i = 0; i < raw.size(); ++i) fold[i] = fuzzy::foldAscii(raw[i]);
        const bool smart = fuzzy::smartCaseWanted(raw);

        // m_view and the scratch buffers are MEMBERS, reused: this runs on every keystroke over a list
        // that can hold tens of thousands of files, so clear() (which keeps capacity) beats fresh locals
        // that would reallocate the whole thing each time.
        m_view.clear();
        m_ranked.clear();
        m_ranked.reserve(rows.size());
        m_results.assign(rows.size(), fuzzy::Result{});

        for (size_t i = 0; i < rows.size(); ++i)
        {
            fuzzy::Result res = fuzzy::score(fold, raw, smart, rows[i].prep);
            if (!res.ok) continue;
            m_results[i] = std::move(res);
            m_ranked.emplace_back(m_results[i].score, i);
        }

        // Total order, so identical keystrokes never reshuffle the list: score, then the shorter
        // primary (a prefix of another entry is the more likely target), then original order.
        std::sort(m_ranked.begin(), m_ranked.end(), [&rows](const auto& a, const auto& b) {
            if (a.first != b.first) return a.first > b.first;
            const size_t la = rows[a.second].primary.length(), lb = rows[b.second].primary.length();
            if (la != lb) return la < lb;
            return a.second < b.second;
        });

        m_view.reserve(m_ranked.size());
        for (const auto& p : m_ranked) m_view.push_back({ p.second, std::move(m_results[p.second]) });

        m_list->SetItemCount(m_view.size());
        m_list->SetSelection(m_view.empty() ? wxNOT_FOUND : 0);
        m_list->RefreshAll();
        m_footer->SetLabel(wxString::Format(_("%zu of %zu"), m_view.size(), rows.size()));
    }

    void move(int delta)
    {
        if (m_view.empty()) return;
        const int last = static_cast<int>(m_view.size()) - 1;
        int sel = m_list->GetSelection();
        if (sel == wxNOT_FOUND) sel = 0;
        sel = std::max(0, std::min(last, sel + delta));
        m_list->SetSelection(sel);
        m_list->ScrollToRow(static_cast<size_t>(sel));
    }

    void commit()
    {
        const int sel = m_list->GetSelection();
        if (sel == wxNOT_FOUND || static_cast<size_t>(sel) >= m_view.size()) return;
        const size_t row = m_view[static_cast<size_t>(sel)].row;
        if (!active()[row].enabled) return;    // a disabled command must not be dispatchable
        m_chosen    = static_cast<int>(row);
        m_chosenAlt = m_inAlt;                 // latched: chosenRow() must not follow a later requery
        EndModal(wxID_OK);
    }

    void onKey(wxKeyEvent& e)
    {
        switch (e.GetKeyCode())
        {
            case WXK_ESCAPE:    m_chosen = -1; EndModal(wxID_CANCEL); return;
            case WXK_RETURN:
            case WXK_NUMPAD_ENTER: commit(); return;
            case WXK_UP:        move(-1); return;
            case WXK_DOWN:      move(+1); return;
            case WXK_PAGEUP:    move(-10); return;
            case WXK_PAGEDOWN:  move(+10); return;
            default: break;
        }
        // Ctrl+P / Ctrl+N, for the muscle memory of everyone who has used this control elsewhere.
        if (e.GetModifiers() == wxMOD_CONTROL)
        {
            if (e.GetKeyCode() == 'P') { move(-1); return; }
            if (e.GetKeyCode() == 'N') { move(+1); return; }
        }
        e.Skip();
    }

    // One ranked entry: which row, and where its characters matched. A single struct rather than two
    // parallel vectors, so "same length, same order" is structural instead of a convention.
    struct ViewRow { size_t row; fuzzy::Result hit; };

    std::vector<FilterRow>    m_rows;
    std::vector<FilterRow>    m_alt;     // the '@' set - materialized lazily, see setAltRows()
    std::vector<ViewRow>      m_view;    // active() rows that matched, ranked best-first
    std::function<std::vector<FilterRow>()> m_altProvider;
    // Scratch, kept as members purely to retain their capacity between keystrokes.
    std::vector<std::pair<int, size_t>> m_ranked;    // (score, row index)
    std::vector<fuzzy::Result>          m_results;   // by row index
    wxFont m_baseFont, m_boldFont;
    wxTextCtrl*   m_field  = nullptr;
    ResultList*   m_list   = nullptr;
    wxStaticText* m_footer = nullptr;
    wxUniChar m_altPrefix = 0;
    bool m_altReady  = false;
    bool m_inAlt     = false;
    bool m_chosenAlt = false;
    int  m_chosen  = -1;
    bool m_dark    = false;
};
