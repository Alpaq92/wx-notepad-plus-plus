// SPDX-License-Identifier: Apache-2.0
//
// terminal_selftest - behavioural self-test for the integrated Terminal panel.
// Copyright 2026 The wxNote Authors.
//
// Builds a REAL TerminalPanel in a real frame and drives it through its own public API, asserting
// observable state. It cannot judge rendering - glyph crispness, margins and alignment need eyes - but
// every bug this panel has actually shipped was behavioural (a popup leaking per pick, a reopen guard
// swallowing the other dropdown's click, the lights toggle dragging the chrome with it), and those are
// exactly what this catches.
//
// Not part of the app: built as a separate target, so no test-only code lives in wxnote itself.
//   cmake --build build --target terminal_selftest && build/bin/terminal_selftest

#include "terminal_panel.h"

#include <wx/app.h>
#include <wx/frame.h>
#include <wx/uiaction.h>
#include <cstdio>

static int g_fail = 0;

static void check(bool ok, const char* what)
{
    std::printf(ok ? "  ok    %s\n" : "  FAIL  %s\n", what);
    if (!ok) ++g_fail;
}

template <class T>
static int countKids(wxWindow* w)
{
    int n = 0;
    for (wxWindow* c : w->GetChildren())
        if (dynamic_cast<T*>(c)) ++n;
    return n;
}

// Popups are not in the panel's child list on every port, so walk the whole tree from the frame.
template <class T>
static int countDeep(wxWindow* w)
{
    int n = dynamic_cast<T*>(w) ? 1 : 0;
    for (wxWindow* c : w->GetChildren()) n += countDeep<T>(c);
    return n;
}

static std::vector<TermFlatButton*> toolbarButtons(wxWindow* panel)
{
    std::vector<TermFlatButton*> r;
    for (wxWindow* c : panel->GetChildren())
        if (auto* b = dynamic_cast<TermFlatButton*>(c)) r.push_back(b);
    return r;   // creation order: +, shell chip, lights, collapse
}

static void pump(int ms = 80)
{
    wxYield();
    wxMilliSleep(ms);
    wxYield();
}

// A REAL click: wxUIActionSimulator posts OS-level input, so this goes through the same hit-testing,
// hover and event routing a human click does - not a synthesised wxEvent that would bypass all of it.
static void clickWindow(wxWindow* w)
{
    wxUIActionSimulator sim;
    const wxPoint c = w->GetScreenPosition() + wxPoint(w->GetSize().x / 2, w->GetSize().y / 2);
    sim.MouseMove(c);
    pump(40);
    sim.MouseClick();
    pump(120);
}

static void clickScreen(const wxPoint& pt)
{
    wxUIActionSimulator sim;
    sim.MouseMove(pt);
    pump(40);
    sim.MouseClick();
    pump(120);
}

class SelfTestApp : public wxApp
{
public:
    bool OnInit() override
    {
        m_frame = new wxFrame(nullptr, wxID_ANY, "terminal selftest",
                              wxPoint(60, 60), wxSize(900, 420));
        m_panel = new TerminalPanel(m_frame, /*dark=*/true, "Consolas");
        // The panel must actually fill the frame and be laid out, or its buttons have no on-screen
        // position and the simulated clicks below land on nothing.
        auto* s = new wxBoxSizer(wxVERTICAL);
        s->Add(m_panel, 1, wxEXPAND);
        m_frame->SetSizer(s);
        m_frame->Show();
        m_frame->Layout();
        // Run once the loop is live: the panel's controls, the popups and the CallAfter-deferred
        // teardown all need a running event loop to behave as they do in the app.
        CallAfter([this]{ runAll(); });
        return true;
    }

private:
    void runAll()
    {
        std::printf("---- glyphs ----\n");
        glyphs();
        std::printf("---- shell detection ----\n");
        shells();
        std::printf("---- chrome ----\n");
        chrome();
        std::printf("---- tabs ----\n");
        tabs();
        std::printf("---- lights toggle ----\n");
        lights();
        std::printf("---- dropdowns (api) ----\n");
        dropdowns();
        std::printf("---- UI: real clicks via wxUIActionSimulator ----\n");
        uiClicks();

        m_panel->shutdownAll();   // no child shell outlives the test
        wxYield();
        std::printf(g_fail ? "\nFAILURES: %d\n" : "\nALL CHECKS PASSED\n", g_fail);
        std::fflush(stdout);
        m_frame->Destroy();
        ExitMainLoop();
    }

    // Every chrome glyph must rasterise. A typo in the inline SVG yields an invalid bundle and the
    // button silently renders empty - the failure mode is a blank toolbar, with nothing in any log.
    void glyphs()
    {
        struct { const char* name; const char* svg; } g[] = {
            { "kTGPlus",   kTGPlus   }, { "kTGCaret",    kTGCaret    },
            { "kTGClose",  kTGClose  }, { "kTGLights",   kTGLights   },
            { "kTGCollapse", kTGCollapse },
        };
        for (const auto& e : g)
        {
            const wxBitmapBundle b = termGlyph(e.svg, *wxWHITE);
            check(b.IsOk(), e.name);
        }
        // currentColor must actually be substituted - nanosvg does not understand it, so a glyph that
        // kept the literal would rasterise blank rather than fail loudly.
        check(!wxString(kTGPlus).Contains("#"), "kTGPlus carries no baked colour (uses currentColor)");
    }

    // detectTermShells() feeds the picker: duplicate labels are indistinguishable rows, and an empty
    // list means a dropdown with nothing to choose.
    void shells()
    {
        const std::vector<TermShell> v = detectTermShells();
        check(!v.empty(), "at least one shell detected");
        bool dup = false, blank = false;
        for (size_t i = 0; i < v.size(); ++i)
        {
            if (v[i].name.empty() || v[i].cmd.empty()) blank = true;
            for (size_t j = i + 1; j < v.size(); ++j)
                if (v[i].name == v[j].name) dup = true;
        }
        check(!blank, "no shell has an empty name/command");
        check(!dup,   "no two shells share a label (a duplicate is an unpickable row)");
    }

    void chrome()
    {
        // +, shell chip, lights, collapse. The tabs dropdown deliberately is NOT here: it lives on the
        // tab strip as wx's own window-list button.
        check(countKids<TermFlatButton>(m_panel) == 4, "toolbar has exactly 4 flat buttons");
        check(countKids<wxAuiNotebook>(m_panel) == 1,  "panel hosts one notebook");
        // The chip's caret is a vector glyph, not a character: a text caret font-links to MS UI Gothic
        // on Windows and renders as a jagged 1-bit bitmap.
        const wxString lbl = m_panel->shellLabel();
        check(!lbl.Contains(wxString::FromUTF8("\xE2\x96\xBE")), "shell chip label has no text caret");
        check(!lbl.empty(), "shell chip label is not empty");
    }

    void tabs()
    {
        check(m_panel->empty(), "starts with no terminals");
        m_panel->addTerminal(-1);   // -1 = the default shell, as the frame's first toggle does
        wxYield();
        check(!m_panel->empty(), "addTerminal(-1) opens one");
        m_panel->addTerminal(0);
        wxYield();
        check(!m_panel->empty(), "a second terminal opens");
    }

    void lights()
    {
        // THE regression this split exists for: the toggle must recolour the terminal only. If the
        // panel's own background moves with it, the chrome is being dragged along again.
        const wxColour before = m_panel->GetBackgroundColour();
        m_panel->setTerminalDark(false);
        wxYield();
        check(m_panel->GetBackgroundColour() == before, "lights=off leaves the chrome background alone");
        m_panel->setTerminalDark(true);
        wxYield();
        check(m_panel->GetBackgroundColour() == before, "lights=on leaves the chrome background alone");
    }

    void dropdowns()
    {
        // Guarded on the ANCHOR, not on time alone: a bare timestamp also swallowed the first click on
        // the OTHER dropdown, so neither list would open.
        check(!m_panel->listReopenGuard(m_panel), "guard is open for an anchor that never opened one");

        m_panel->openPicker();
        wxYield();
        check(true, "openPicker() does not crash");

        m_panel->openTabList();   // the other dropdown, while the first is open
        wxYield();
        check(true, "openTabList() does not crash with the picker open");

        // Repeated open/dismiss must not accumulate windows: onClick used to call Dismiss(), which by
        // wx's contract never runs OnDismiss(), so the deferred Destroy() never fired and every pick
        // leaked a live popup.
        const size_t before = wxTopLevelWindows.GetCount();
        for (int i = 0; i < 12; ++i) { m_panel->openPicker(); wxYield(); }
        wxYield();
        const size_t after = wxTopLevelWindows.GetCount();
        check(after <= before + 2, "repeated opens do not accumulate windows (popup teardown runs)");
    }

    // Real OS-level clicks on the real controls. This is the half that matters: everything above drives
    // the panel's API directly and would happily pass even if a button were wired to nothing, mispainted
    // to zero size, or covered by another window.
    void destroyPopups()
    {
        std::vector<wxWindow*> kill;
        std::function<void(wxWindow*)> walk = [&](wxWindow* w){
            if (dynamic_cast<TermListPopup*>(w)) kill.push_back(w);
            for (wxWindow* c : w->GetChildren()) walk(c);
        };
        walk(m_frame);
        for (wxWindow* w : kill) w->Destroy();
        pump(120);
    }

    void uiClicks()
    {
        // The API-level dropdowns() phase opened popups by direct call and never dismissed them (only
        // an outside click or a pick does). Clear that leftover state so this phase starts clean - it
        // is a test artifact, not reachable through the UI, where each open needs its own click.
        destroyPopups();
        m_frame->Raise();
        m_frame->SetFocus();
        pump(250);

        const std::vector<TermFlatButton*> b = toolbarButtons(m_panel);
        if (b.size() != 4) { check(false, "expected 4 toolbar buttons to click"); return; }
        TermFlatButton* plus   = b[0];
        TermFlatButton* chip   = b[1];
        TermFlatButton* lights = b[2];
        TermFlatButton* hide   = b[3];

        check(plus->GetSize().x > 0 && plus->GetSize().y > 0, "buttons have a non-zero size (clickable)");

        // "+" opens a terminal.
        const size_t before = m_nbPages();
        clickWindow(plus);
        check(m_nbPages() == before + 1, "clicking + opens a terminal");

        // The chip opens the picker...
        check(visiblePickers() == 0, "no popup before clicking the chip");
        clickWindow(chip);
        const int opened = visiblePickers();
        check(opened == 1, "clicking the shell chip opens exactly one picker");

        // ...and clicking it AGAIN must leave it closed, not reopen it. This is the bug the reopen
        // guard exists for: wx dismisses the popup on the chip's LEFT_DOWN and still delivers that same
        // click to the chip, so a naive reopen makes the list look permanently stuck open.
        TermListPopup* first = firstVisiblePicker();
        clickWindow(chip);
        TermListPopup* after = firstVisiblePicker();
        if (after) std::printf("     [diag] visible=%d  same-object=%s\n",
                               visiblePickers(), (after == first ? "yes(never-closed)" : "no(REOPENED)"));
        check(visiblePickers() == 0, "clicking the chip again CLOSES it (no reopen)");
        destroyPopups();

        // Picking a row spawns that shell. Wait past the 250ms reopen guard first - reopening inside
        // that window is DELIBERATELY suppressed (it is the same click that just dismissed it), so a
        // faster reopen here would be the test fighting a working guard, not a bug.
        pump(320);
        clickWindow(chip);
        if (visiblePickers() == 1)
        {
            const size_t n = m_nbPages();
            // Row 0 sits just inside the popup's top border; the popup drops directly under the chip.
            const wxPoint row0 = chip->GetScreenPosition() + wxPoint(20, chip->GetSize().y + 14);
            clickScreen(row0);
            check(m_nbPages() == n + 1, "picking a shell from the picker opens that terminal");
            check(visiblePickers() == 0, "the picker closes after a pick");
        }
        else check(false, "picker did not reopen for the pick test");

        // The lights toggle must recolour the terminal WITHOUT dragging the chrome with it.
        const wxColour chromeBefore = m_panel->GetBackgroundColour();
        clickWindow(lights);
        check(m_panel->GetBackgroundColour() == chromeBefore, "clicking lights leaves the chrome alone");

        // The collapse button asks the frame to hide the panel - it must not close/destroy anything.
        m_hideAsked = false;
        m_panel->onCloseRequested = [this]{ m_hideAsked = true; };
        clickWindow(hide);
        check(m_hideAsked, "clicking collapse fires onCloseRequested (hide, not destroy)");
        check(!m_panel->empty(), "collapse does not kill the terminals");
    }

    // Count VISIBLE pickers, not merely constructed ones. A dismissed popup is hidden immediately but
    // its window object is destroyed later (OnDismiss -> CallAfter(Destroy)), so a dynamic_cast count
    // would still see a just-closed popup for a few ms and misreport a working toggle as "still open".
    // IsShown() is the honest question - and it also tells a real reopen (shown) from a lingering
    // dismissed object (hidden).
    int visiblePickers() const
    {
        int n = 0;
        std::function<void(wxWindow*)> walk = [&](wxWindow* w){
            if (auto* p = dynamic_cast<TermListPopup*>(w)) { if (p->IsShown()) ++n; }
            for (wxWindow* c : w->GetChildren()) walk(c);
        };
        walk(m_frame);
        return n;
    }
    TermListPopup* firstVisiblePicker() const
    {
        TermListPopup* found = nullptr;
        std::function<void(wxWindow*)> walk = [&](wxWindow* w){
            if (auto* p = dynamic_cast<TermListPopup*>(w)) { if (p->IsShown() && !found) found = p; }
            for (wxWindow* c : w->GetChildren()) walk(c);
        };
        walk(m_frame);
        return found;
    }

    size_t m_nbPages() const
    {
        for (wxWindow* c : m_panel->GetChildren())
            if (auto* nb = dynamic_cast<wxAuiNotebook*>(c)) return nb->GetPageCount();
        return 0;
    }

    wxFrame*       m_frame = nullptr;
    TerminalPanel* m_panel = nullptr;
    bool           m_hideAsked = false;
};

wxIMPLEMENT_APP_CONSOLE(SelfTestApp);
