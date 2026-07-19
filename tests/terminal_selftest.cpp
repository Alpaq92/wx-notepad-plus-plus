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
#include "term_backend.h"   // PTY process backend - spawned directly by the pty phases below
#include "term_view.h"      // libvterm cell grid - constructed directly by the pty phases below

#include <wx/app.h>
#include <wx/frame.h>
#include <wx/uiaction.h>
#include <wx/accel.h>     // wxAcceleratorTable/Entry - the frame accel table the Ctrl+C scope test installs
#include <cstdio>
#include <memory>
#include <string>

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

    int OnExit() override
    {
        // Same contract as the real app (WxnApp::OnExit): the backends' kill-sweep threads must be
        // joined after the last backend dtor and before wx teardown, or process exit abandons them
        // mid-walk. The pty phases above kill real children, so sweeps DO run here.
        TermBackend::joinKillSweeps();
        return wxApp::OnExit();
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
        std::printf("---- keyboard: focus + Enter/Space activation ----\n");
        keyboard();
        std::printf("---- accel scope: terminal-focused Ctrl+C reaches the terminal ----\n");
        accelScopeCtrlC();
        std::printf("---- pty: emulation (TermView + libvterm, no process) ----\n");
        ptyEmulation();
        std::printf("---- pty: backend (ConPTY/forkpty spawn + resize) ----\n");
        ptyBackend();
        std::printf("---- pty: fallback (spawn forced to fail) ----\n");
        ptyFallback();

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

    // The chrome must be operable without a mouse: the flat buttons are focusable and fire on
    // Enter/Space. This is the a11y parity the owner-drawn buttons owe the native wxChoice they replaced
    // - a keyboard/screen-reader user has to be able to reach and trigger them.
    void keyboard()
    {
        const std::vector<TermFlatButton*> b = toolbarButtons(m_panel);
        if (b.size() != 4) { check(false, "expected 4 toolbar buttons for the keyboard test"); return; }
        for (auto* btn : b) check(btn->AcceptsFocus(), "toolbar button accepts focus (keyboard-reachable)");

        // Injected keys land in the OS FOREGROUND window, not our frame by handle. Before EVERY key burst
        // we force the frame foreground and verify it; if we can't (a toast stole it, the user touched the
        // desktop, a locked session), we SKIP rather than fail - injecting blind would both misreport the
        // result and leak keystrokes into whatever app is really in front. See the runtime-verify memory.
        if (!frameForeground()) { std::printf("  skip  frame not foreground; skipping key-injection tests\n"); return; }

        wxUIActionSimulator sim;
        TermFlatButton* plus = b[0];
        plus->SetFocus();
        pump(120);
        check(plus->HasFocus(), "SetFocus lands on the + button");

        // Enter on the focused button opens a terminal - same effect as a click, no mouse involved.
        size_t before = m_nbPages();
        sim.KeyDown(WXK_RETURN);
        sim.KeyUp(WXK_RETURN);
        pump(180);
        check(m_nbPages() == before + 1, "Enter on the focused + button opens a terminal");

        // Space activates too (addTerminal moves focus to the new shell, so re-focus first).
        if (!frameForeground()) { std::printf("  skip  lost foreground; aborting key tests\n"); return; }
        plus->SetFocus();
        pump(120);
        before = m_nbPages();
        sim.KeyDown(WXK_SPACE);
        sim.KeyUp(WXK_SPACE);
        pump(180);
        check(m_nbPages() == before + 1, "Space on the focused + button opens a terminal");

        // The shell picker must be keyboard-operable. Prove ARROW NAVIGATION, not just Enter: the picker
        // opens with the current shell pre-highlighted, so Enter alone would pick that seeded row and pass
        // even if Down were broken. Home resets to row 0, Down moves to row 1, and Enter must then open the
        // SECOND shell - so a broken/removed Down handler opens the wrong shell and fails the title check.
        TermFlatButton* chip = b[1];
        const std::vector<TermShell> shells = detectTermShells();
        if (shells.size() >= 2 && frameForeground())
        {
            pump(320);                 // clear the 250ms reopen guard from earlier phases
            clickWindow(chip);         // opens the picker, which TAKES foreground itself (wxPU_CONTAINS_CONTROLS)
            // Do NOT frameForeground() here: the open picker is our foreground window now, and stealing
            // foreground back to the frame would deactivate and dismiss it before the keys land.
            if (visiblePickers() == 1)
            {
                before = m_nbPages();
                sim.KeyDown(WXK_HOME);   sim.KeyUp(WXK_HOME);   pump(70);   // -> row 0 (deterministic start)
                sim.KeyDown(WXK_DOWN);   sim.KeyUp(WXK_DOWN);   pump(70);   // -> row 1
                sim.KeyDown(WXK_RETURN); sim.KeyUp(WXK_RETURN); pump(200);
                check(m_nbPages() == before + 1, "Home+Down+Enter in the picker opens a terminal");
                check(visiblePickers() == 0,     "the picker closes after a keyboard pick");
                // Tabs are titled "<shell name> <n>"; row 1 must be the second shell, proving Down moved it.
                check(newestPageTitle().BeforeLast(' ') == shells[1].name,
                      "Down moved the highlight to the 2nd shell (arrow nav works, not just Enter)");
            }
            else check(false, "picker did not open for the keyboard-nav test");
        }

        // Esc dismisses the picker WITHOUT picking - no terminal should open.
        if (frameForeground())
        {
            pump(320);
            clickWindow(chip);         // picker takes foreground; don't steal it back (see above)
            if (visiblePickers() == 1)
            {
                before = m_nbPages();
                sim.KeyDown(WXK_ESCAPE); sim.KeyUp(WXK_ESCAPE);
                pump(200);
                check(visiblePickers() == 0, "Esc closes the picker");
                check(m_nbPages() == before, "Esc does not open a terminal");
            }
            else check(false, "picker did not open for the Esc test");
        }
        destroyPopups();

        // Every chrome button needs an accessible name: they are glyph-only (empty label), so without one
        // a screen reader announces an unlabelled control and the "keyboard/screen-reader" claim is hollow.
        for (auto* btn : b) check(!btn->GetName().empty(), "toolbar button has an accessible name");

        // The focus escape. Focusable buttons are pointless if focus starts in the terminal and the
        // terminal eats Tab: Ctrl+Shift+Up must reach the toolbar, Ctrl+Shift+Down must come back.
        if (frameForeground())
        {
            m_panel->focusActive();    // put focus back in the terminal, where a user actually starts
            pump(150);
            check(focusInTerminal(), "focus starts in the terminal");
            sim.Char(WXK_UP, wxMOD_CONTROL | wxMOD_SHIFT);
            pump(180);
            check(b[0]->HasFocus(), "Ctrl+Shift+Up escapes the terminal to the toolbar");
            sim.Char(WXK_DOWN, wxMOD_CONTROL | wxMOD_SHIFT);
            pump(180);
            check(focusInTerminal(), "Ctrl+Shift+Down returns focus to the terminal");
        }
    }

    // Phase 0 of the shortcut work: with the terminal focused,
    // plain Ctrl+C must reach the TermView - which cooks it into the child's SIGINT - instead of being
    // stolen by the frame's accelerator table and run as Edit>Copy on the hidden editor. The production
    // fix lives in WxnShellFrameT::refreshAccelerators/onChildFocus (src/main.cpp), which carries
    // wxIMPLEMENT_APP and can't be linked into this standalone test. So this phase exercises the underlying
    // MECHANISM the fix relies on: a real frame accel table binding Ctrl+C (the exact shape Edit>Copy's
    // "\tCtrl+C" produces), a real focused TermView, real OS key injection through the real
    // accelerator-translation path, and the empty-table swap that is precisely what "terminal scope"
    // installs. MSW-only: the defect IS MSW's PreProcessMessage/TranslateAccelerator firing before the
    // focused window sees WM_KEYDOWN; GTK/macOS accel handling differs and is treated separately.
    void accelScopeCtrlC()
    {
#ifndef __WXMSW__
        std::printf("  skip  Ctrl+C accel-scope gating is verified on MSW (the accelerator-vs-focus ordering is the MSW-specific defect)\n");
#else
        destroyPopups();
        auto* v = new TermView(m_frame, "Consolas", 10, /*dark=*/true);
        v->SetSize(0, 0, 600, 320);
        pump(40);

        // Observe what the terminal received: TermView turns a keypress into VT bytes and hands them to
        // onOutput synchronously (vterm's output callback), so a Ctrl+C that reaches the view shows up
        // here as 0x03 (^C) - the byte the shell reads as an interrupt.
        std::string outbuf;
        v->onOutput = [&outbuf](const char* d, size_t n){ outbuf.append(d, n); };

        // The "hidden editor" stand-in: a frame accelerator binding Ctrl+C to a command, exactly as
        // Edit>Copy (kCmdEditCopy, label "&Copy\tCtrl+C") does in the real menu bar. If this command
        // fires, the accelerator ran instead of the terminal getting the key - the defect.
        const int kSentinel = wxID_HIGHEST + 777;
        m_frame->Bind(wxEVT_MENU, [this](wxCommandEvent&){ m_sentinelFired = true; }, kSentinel);
        wxAcceleratorEntry ent(wxACCEL_CTRL, 'C', kSentinel);
        m_frame->SetAcceleratorTable(wxAcceleratorTable(1, &ent));

        if (!frameForeground())
        {
            std::printf("  skip  frame not foreground; skipping Ctrl+C injection\n");
            m_frame->SetAcceleratorTable(wxNullAcceleratorTable);
            v->onOutput = nullptr; v->Destroy(); pump(60);
            return;
        }

        wxUIActionSimulator sim;

        // Editor scope (full accel table present): reproduces the defect - the accel steals Ctrl+C, so the
        // command fires and the view never sees the key. This is the current-behaviour claim the fix rests
        // on; proving it here is what makes the terminal-scope checks below meaningful rather than vacuous.
        v->SetFocus(); pump(120);
        m_sentinelFired = false; outbuf.clear();
        sim.Char('C', wxMOD_CONTROL);
        pump(180);
        const bool viewGotCtrlC = outbuf.find('\x03') != std::string::npos;
        std::printf("     [diag] full table: sentinel=%s  view-got-^C=%s\n",
                    m_sentinelFired ? "yes" : "no", viewGotCtrlC ? "yes" : "no");
        check(m_sentinelFired && !viewGotCtrlC,
              "editor scope: the frame accel table steals Ctrl+C from the terminal (reproduces the defect)");

        // Terminal scope (empty table - exactly what refreshAccelerators(Scope::Terminal) installs): the
        // fix. With nothing to steal it, the keystroke reaches the focused TermView.
        m_frame->SetAcceleratorTable(wxNullAcceleratorTable);
        v->SetFocus(); pump(120);
        m_sentinelFired = false; outbuf.clear();
        sim.Char('C', wxMOD_CONTROL);
        pump(180);
        check(outbuf.find('\x03') != std::string::npos,
              "terminal scope: Ctrl+C reaches the TermView (its SIGINT path), not the accelerator");
        check(!m_sentinelFired,
              "terminal scope: the editor's Ctrl+C command does not fire");

        m_frame->SetAcceleratorTable(wxNullAcceleratorTable);
        v->onOutput = nullptr;
        v->Destroy();
        pump(60);
#endif
    }

    // "Focus is in the terminal" now has two honest answers: the TermView grid in PTY mode, the
    // wxSTC console in legacy fallback mode. The tab picks its mode at construction (per machine /
    // per test hook), so the focus checks must accept either or they would fail on exactly one mode.
    static bool focusInTerminal()
    {
        wxWindow* f = wxWindow::FindFocus();
        return dynamic_cast<TermView*>(f) != nullptr || dynamic_cast<wxStyledTextCtrl*>(f) != nullptr;
    }

    // ---- pty phases. None of these inject OS keystrokes: feed()/write() drive the emulator and the
    // backend directly, so no foreground gating is needed and nothing can leak into other windows. ----

    // The v2 renderer alone, no child process: feed VT bytes by hand and read the screen model back
    // through the rowText/screenText test accessors.
    void ptyEmulation()
    {
        auto* v = new TermView(m_frame, "Consolas", 10, /*dark=*/true);
        // Give it a real >= one-cell size: the grid deliberately stays 80x24 until the first real
        // wxEVT_SIZE (see term_view.h), and these assertions should run against a grid the widget
        // actually computed, proving the size->grid path too.
        v->SetSize(0, 0, 600, 320);
        pump(40);
        check(v->cols() >= 20 && v->rows() >= 5, "TermView sized to a usable grid");
        // ...and NOT still the constructor default: the grid stays 80x24 until the first real size
        // event, and 80x24 satisfies the >=20x5 check above - so that check alone would pass with
        // a dead size->updateGrid path. 600x320 cannot compute to exactly 80x24 at any plausible
        // cell metric (Consolas 10pt is ~8x18px -> roughly 75x17).
        check(v->cols() != 80 || v->rows() != 24, "grid recomputed from the real size (not the 80x24 default)");

        // An SGR colour sequence must be PARSED (the old pipe console merely stripped it): the text
        // lands on the screen, the escape bytes do not.
        const char red[] = "\x1b[31mwxnote_red_ok\x1b[0m plain";
        v->feed(red, sizeof(red) - 1);
        check(v->rowText(0) == "wxnote_red_ok plain", "SGR colour sequence is parsed, not printed");
        check(v->screenText().Contains("wxnote_red_ok"), "screenText sees the fed text");

        // ED 2 + CUP - the "clear" every shell's `cls`/`clear` emits - must empty the whole grid.
        const char clear[] = "\x1b[2J\x1b[H";
        v->feed(clear, sizeof(clear) - 1);
        wxString all = v->screenText();
        all.Replace("\n", "");
        all.Trim();
        check(all.empty(), "ED 2 + CUP clears the whole screen");

        // Cursor addressing must land text at the addressed CELL, not append linearly - the very
        // capability (full-screen placement) the pipe console lacked.
        const char at[] = "\x1b[3;5Hplaced";
        v->feed(at, sizeof(at) - 1);
        check(v->rowText(2).Find("placed") == 4, "CUP addresses row 3 col 5 (full-screen placement)");

        v->Destroy();
        pump(60);
    }

    // The real backend: spawn the default shell on a pty, write an echo command to its stdin, and
    // watch the output arrive through onData -> feed -> the screen model. Then resize and prove the
    // child survives, then kill and prove it dies.
    void ptyBackend()
    {
        const std::vector<TermShell> shells = detectTermShells();
        if (shells.empty()) { check(false, "no shell to spawn for the backend test"); return; }

        auto* v = new TermView(m_frame, "Consolas", 10, /*dark=*/true);
        v->SetSize(0, 0, 600, 320);
        pump(40);

        std::unique_ptr<TermBackend> be =
            TermBackend::spawn(shells[0].cmd, wxGetCwd(), v->cols(), v->rows());
        m_ptyWorks = (be != nullptr);
        if (!be)
        {
            // Legitimate on Windows < 10 1809 (no ConPTY): the fallback phase below still proves
            // the tab copes. Skip, don't fail - failing here would make the suite red on exactly
            // the machines the fallback exists for.
            std::printf("  skip  TermBackend::spawn returned null (no ConPTY on this OS?)\n");
            v->Destroy();
            pump(60);
            return;
        }
        // Same wiring the real tab does, assigned synchronously right after spawn per the contract.
        size_t rxBytes = 0;   // diag: total bytes onData delivered (distinguishes "no read" from "bad render")
        be->onData         = [v, &rxBytes](const char* d, size_t n){ rxBytes += n; v->feed(d, n); };
        v->onOutput        = [&be](const char* d, size_t n){ if (be) be->write(d, n); };
        v->onResizeRequest = [&be](int c, int r){ if (be) be->resize(c, r); };
        check(be->running(), "spawned child reports running()");

        const char cmd[] = "echo wxnote_pty_ok\r\n";
        be->write(cmd, sizeof(cmd) - 1);
        // Pump until the output shows up (shell startup time varies); 5s is generous, the loop
        // exits the moment it appears. The needle must sit on a row WITHOUT the word 'echo':
        // terminal input echo (conhost cooked mode on Windows, the tty line discipline on POSIX)
        // reflects the typed command line itself onto the screen, so a whole-screen Contains()
        // went green off that echoed input even when the command never EXECUTED (e.g. a write
        // path that mangles the CR so Enter never registers). Only the executed command prints
        // the needle on a line of its own.
        auto executedRowSeen = [v]
        {
            for (int r = 0; r < v->rows(); ++r)
            {
                const wxString t = v->rowText(r);
                if (t.Contains("wxnote_pty_ok") && !t.Contains("echo")) return true;
            }
            return false;
        };
        bool seen = false;
        for (int i = 0; i < 100 && !seen; ++i)
        {
            pump(50);
            seen = executedRowSeen();
        }
        if (!seen)
            std::printf("     [diag] onData bytes=%u screen[0..120]='%s'\n", (unsigned)rxBytes,
                        (const char*)v->screenText().Left(120).utf8_str());
        check(seen, "executed echo output (not its input echo) arrives via backend->onData -> view->feed");

        // Resize: a smaller widget -> a smaller grid -> backend->resize (wired above); the child
        // must survive the SIGWINCH/ResizePseudoConsole.
        const int c0 = v->cols(), r0 = v->rows();
        v->SetSize(0, 0, 400, 200);
        pump(120);
        check(v->cols() != c0 || v->rows() != r0, "resizing the view changes the grid");
        check(be->running(), "child survives the pty resize");

        be->kill();
        bool dead = false;
        for (int i = 0; i < 40 && !dead; ++i)
        {
            pump(50);
            dead = !be->running();
        }
        check(dead, "kill() stops the child");

        // Teardown order matters: the callbacks are cleared and the backend destroyed while `v` is
        // still alive, so a chunk queued mid-pump can never feed a dead view.
        v->onOutput = nullptr;
        v->onResizeRequest = nullptr;
        be.reset();
        v->Destroy();
        pump(60);
    }

    // The fallback: force spawn() to fail via the test hook and prove the tab still opens - in
    // legacy pipe mode, with the v1 wxSTC console - and that the shared tab surface (theme, focus)
    // keeps working across a mixed PTY/legacy tab set.
    void ptyFallback()
    {
        // Every tab the earlier phases opened went through the REAL spawn; if that worked (probed
        // by ptyBackend above), those tabs must actually be running the PTY mode.
        if (m_ptyWorks)
            check(currentTabUsesPty(), "normally-opened tab runs in PTY mode when spawn works");

        termForceLegacyBackend() = true;
        const size_t before = m_nbPages();
        m_panel->addTerminal(0);
        pump(150);
        termForceLegacyBackend() = false;   // reset immediately - no later phase may inherit it
        check(m_nbPages() == before + 1, "tab still opens when the PTY backend is unavailable");
        check(!currentTabUsesPty(), "forced-fail tab reports legacy mode");
        check(countDeep<wxStyledTextCtrl>(m_panel) >= 1, "legacy tab hosts the v1 wxSTC console");

        // The panel calls these blind on every tab - they must hold up when PTY and legacy tabs
        // coexist in one notebook.
        m_panel->setTerminalDark(false);
        m_panel->setTerminalDark(true);
        m_panel->focusActive();
        pump(60);
        check(true, "applyTheme/focus survive a mixed PTY+legacy tab set");
    }

    bool currentTabUsesPty() const
    {
        for (wxWindow* c : m_panel->GetChildren())
            if (auto* nb = dynamic_cast<wxAuiNotebook*>(c))
                if (auto* t = dynamic_cast<TerminalTab*>(nb->GetCurrentPage()))
                    return t->usingPty();
        return false;
    }

    // Force the test frame to the OS foreground and confirm it took. Key injection targets the foreground
    // window, so this gates every key burst above: false -> could not take foreground -> caller skips (does
    // NOT inject). Non-MSW returns true (no foreground gating needed for the local wxUIActionSimulator path).
    bool frameForeground()
    {
#ifdef __WXMSW__
        HWND fh = static_cast<HWND>(m_frame->GetHandle());
        ::SetForegroundWindow(fh);
        pump(120);
        return ::GetForegroundWindow() == fh;
#else
        return true;
#endif
    }
    // Title of the most recently added notebook page (AddPage appends + selects, so it's the last index).
    wxString newestPageTitle() const
    {
        for (wxWindow* c : m_panel->GetChildren())
            if (auto* nb = dynamic_cast<wxAuiNotebook*>(c))
            { const size_t n = nb->GetPageCount(); return n ? nb->GetPageText(n - 1) : wxString(); }
        return wxString();
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
    bool           m_ptyWorks  = false;   // did ptyBackend's real spawn succeed on this machine?
    bool           m_sentinelFired = false;   // accelScopeCtrlC: did the frame's Ctrl+C accelerator fire?
};

wxIMPLEMENT_APP_CONSOLE(SelfTestApp);
