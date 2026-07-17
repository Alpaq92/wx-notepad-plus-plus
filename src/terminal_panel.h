#pragma once
// terminal_panel.h - the integrated terminal panel (View > Show Terminal): a bottom wxAui pane holding
// a wxAuiNotebook of shell tabs, with a "+" button and a shell picker (Windows: cmd / PowerShell /
// pwsh-if-installed / Cygwin-if-installed / WSL-if-installed; Linux: $SHELL (+bash); macOS: zsh/bash).
//
// v1 BACKEND IS A PIPE CONSOLE, deliberately: the child runs with redirected stdin/stdout/stderr
// (wxProcess::Redirect), NOT a real TTY. That covers the NppExec-style 80% (git, compilers, scripts,
// dir/ls, simple REPLs) but not full-screen TUI apps (vim/htop) or shells' native line editing - the
// researched v2 upgrade (ConPTY on Windows + forkpty on Unix feeding a vendored MIT libvterm into a
// cell-grid renderer) slots in behind this same panel/tab/picker UI without changing it. ANSI escape
// sequences in the output are stripped rather than rendered for now, so coloured tools stay readable.
#include <wx/wx.h>
#include <wx/aui/auibook.h>
#include <wx/stc/stc.h>
#include <wx/process.h>
#include <wx/utils.h>
#include <wx/filename.h>
#include <wx/regex.h>
#include <wx/popupwin.h>   // wxPopupTransientWindow - the themed shell picker (native combo popups can't be dark-themed on MSW)
#include <wx/bmpbndl.h>    // wxBitmapBundle - the toolbar's SVG glyphs (see termGlyph)
#include <wx/graphics.h>   // wxGraphicsContext - antialiasing the hover pill (GDI can't; see onPaint)
#include <memory>
#include <wx/time.h>       // wxGetUTCTimeMillis - the picker's reopen guard (see TerminalPanel::openPicker)
#include <wx/settings.h>   // wxSystemSettings - the UI (chrome) font, distinct from the monospace terminal font
#include <algorithm>
#include <functional>
#include <vector>
#include <thread>
#ifdef __WXMSW__
    #include <windows.h>   // GetOEMCP - cmd/PowerShell pipe output arrives in the OEM codepage
#endif

static const int myID_VIEW_TERMINAL = 60200;   // View > Show Terminal (app-local; clear of 60xxx app ids, 61xxx doc-list, 62xxx macros, 63xxx Nib commands)

struct TermShell
{
    wxString name;   // picker label ("cmd", "PowerShell", ... - tool proper nouns, untranslated)
    wxString cmd;    // full command line to spawn
    bool     oem;    // output arrives in the OEM codepage (Windows console tools) instead of UTF-8
};

inline std::vector<TermShell> detectTermShells()
{
    std::vector<TermShell> v;
#ifdef __WXMSW__
    wxString comspec; wxGetEnv("COMSPEC", &comspec); if (comspec.empty()) comspec = "cmd.exe";
    v.push_back({ "cmd", comspec + " /K chcp 65001>nul", false });   // flip the session to UTF-8 so non-ASCII output isn't mojibake
    v.push_back({ "PowerShell", "powershell.exe -NoLogo", true });
    { wxPathList pl; pl.AddEnvList("PATH");
      if (!pl.FindAbsoluteValidPath("pwsh.exe").empty()) v.push_back({ "pwsh", "pwsh.exe -NoLogo", true }); }
    for (const wxString& root : { wxString("C:\\cygwin64"), wxString("C:\\cygwin") })
        if (wxFileExists(root + "\\bin\\bash.exe")) { v.push_back({ "Cygwin", root + "\\bin\\bash.exe --login", false }); break; }
    { wxString win; wxGetEnv("SystemRoot", &win);
      if (!win.empty() && wxFileExists(win + "\\System32\\wsl.exe")) v.push_back({ "WSL", "wsl.exe", false }); }
#else
    // POSIX (Linux + macOS - one branch: both answer to $SHELL and PATH). $SHELL leads, being the user's
    // own choice, then every common shell this machine ACTUALLY has, probed on PATH the same way
    // detectLinuxTerminalEmulators() below probes terminal apps. This used to assume a hardcoded pair
    // (zsh+bash on macOS, $SHELL+bash on Linux) and probe for nothing, so a machine with fish or
    // nushell installed offered neither, and a stock Ubuntu ($SHELL=/bin/bash) offered a one-row
    // dropdown - while Windows enumerated up to five.
    wxPathList pl; pl.AddEnvList("PATH");
    // FindAbsoluteValidPath() searches by BASENAME even when handed an absolute path (wx's own doc:
    // "search for the file name and ignore the path part"), so an absolute $SHELL that isn't itself on
    // PATH needs a direct existence+exec check or a perfectly good shell is silently dropped.
    auto resolve = [&](const wxString& exe) -> wxString {
        wxFileName fn(exe);
        if (fn.IsAbsolute()) return (wxFileExists(exe) && wxIsExecutable(exe)) ? exe : wxString();
        return pl.FindAbsoluteValidPath(exe);
    };
    // Deduped by BASENAME, never by path string: /bin/bash and /usr/bin/bash are the SAME binary on any
    // usrmerge distro, so comparing raw strings (as this did) listed "bash" twice - two identical,
    // unpickable rows. That is precisely the trap the terminal-app list below documents; it just was
    // never applied here.
    auto add = [&](const wxString& path) {
        if (path.empty()) return;
        const wxString label = wxFileNameFromPath(path);
        for (const TermShell& s : v) if (s.name == label) return;
        v.push_back({ label, path, false });
    };
    { wxString sh; if (wxGetEnv("SHELL", &sh) && !sh.empty()) add(resolve(sh)); }
    for (const char* c : { "zsh", "bash", "fish", "nu", "ksh", "tcsh", "dash", "sh" }) add(resolve(c));
    if (v.empty()) v.push_back({ "sh", "/bin/sh", false });   // nothing resolved: the one shell POSIX guarantees
#endif
    return v;
}

// ---- "Open Containing Folder" external tools -----------------------------------------------------
// A DIFFERENT concept from detectTermShells() above: that picks a SHELL PROCESS to run inside our OWN
// embedded Terminal panel; this finds standalone TERMINAL APPLICATIONS to spawn at a folder (used by
// File > Open Containing Folder). label = menu text (untranslated proper noun, like the shell picker);
// launch = what spawnOpenHereTool() (main.cpp) hands to the OS - a full command line on Windows, a bare
// executable name on Linux, an app name on macOS for `open -a`.
struct OpenHereTool { wxString label; wxString launch; };

// Windows: cmd/PowerShell stay STATIC menu items with their original frozen command ids (real Notepad++
// plugins may invoke them via NPPM_MENUCOMMAND - every IDM_* must keep landing on a real menu item).
// This returns only the EXTRA shells this machine happens to have, for the submenu's DynamicSlot -
// reuses detectTermShells()'s exact detection + command strings so the two features never disagree
// about what's installed.
inline std::vector<OpenHereTool> detectWindowsExtraOpenHereTools()
{
    std::vector<OpenHereTool> v;
    for (const TermShell& s : detectTermShells())
        if (s.name != "cmd" && s.name != "PowerShell") v.push_back({ s.name, s.cmd });
    return v;
}
#if !defined(__WXMSW__) && !defined(__WXMAC__)
// Linux has no single "the default terminal" API the way Explorer/Finder exist on the other platforms,
// so instead of guessing one and hoping, list every terminal emulator actually found on PATH - the
// menu then reflects what THIS machine has, not a blind cascade. $TERMINAL (if set and resolvable)
// leads the list since it's the user's explicit override.
inline std::vector<OpenHereTool> detectLinuxTerminalEmulators()
{
    std::vector<OpenHereTool> found;
    wxPathList pl; pl.AddEnvList("PATH");
    // wxPathList::FindValidPath, given an ABSOLUTE input, discards the directory and only checks
    // whether a file with the same BASENAME exists somewhere on PATH ("search for the file name
    // and ignore the path part" - wx's own doc comment) - it never checks the literal path. So a
    // bare command name goes through PATH search as normal, but an absolute $TERMINAL (e.g. an
    // AppImage or manually-installed binary not itself on PATH) needs a direct existence+exec
    // check instead, or a valid, real terminal would be silently dropped from the menu.
    auto has = [&](const wxString& exe) {
        wxFileName fn(exe);
        return fn.IsAbsolute() ? (wxFileExists(exe) && wxIsExecutable(exe)) : !pl.FindAbsoluteValidPath(exe).empty();
    };
    { wxString t; if (wxGetEnv("TERMINAL", &t) && !t.empty() && has(t))
        { wxString lbl = wxFileNameFromPath(t); lbl.Replace("&", "&&"); found.push_back({ lbl, t }); } }
    static const struct { const char* exe; const char* label; } table[] = {
        { "gnome-terminal", "GNOME Terminal" }, { "konsole", "Konsole" }, { "xfce4-terminal", "Xfce Terminal" },
        { "mate-terminal", "MATE Terminal" }, { "tilix", "Tilix" }, { "kitty", "kitty" },
        { "alacritty", "Alacritty" }, { "xterm", "xterm" },
    };
    for (const auto& c : table)
    {
        if (!has(c.exe)) continue;
        // Compare basenames, not raw launch strings - an absolute-path $TERMINAL (e.g.
        // "/usr/bin/kitty") must be recognised as the same binary as the table's bare "kitty"
        // probe, or both end up listed as separate, visually-identical entries.
        bool dup = false; for (const auto& f : found) if (wxFileNameFromPath(f.launch) == c.exe) { dup = true; break; }
        if (!dup) found.push_back({ c.label, c.exe });
    }
    return found;
}
#elif defined(__WXMAC__)
// Terminal.app ships with every Mac; iTerm is the one other terminal common enough to worth a direct
// probe rather than a long candidate table (unlike Linux, macOS third-party terminals are rare).
inline std::vector<OpenHereTool> detectMacTerminalApps()
{
    std::vector<OpenHereTool> found;
    found.push_back({ "Terminal", "Terminal" });
    if (wxDirExists("/Applications/iTerm.app")) found.push_back({ "iTerm", "iTerm" });
    return found;
}
#endif

class TerminalTab : public wxPanel
{
public:
    TerminalTab(wxWindow* parent, const TermShell& shell, const wxString& cwd, bool dark, const wxString& fontFace)
        : wxPanel(parent), m_oem(shell.oem)
    {
        auto* s = new wxBoxSizer(wxVERTICAL);
        m_out = new wxStyledTextCtrl(this, wxID_ANY);
        for (int i = 0; i < 5; ++i) m_out->SetMarginWidth(i, 0);
        m_out->SetWrapMode(wxSTC_WRAP_CHAR);
        m_out->SetUseHorizontalScrollBar(false);
        m_out->StyleSetFaceName(wxSTC_STYLE_DEFAULT, fontFace.empty() ? wxString("Consolas") : fontFace);
        m_out->StyleSetSize(wxSTC_STYLE_DEFAULT, 10);
        applyTheme(dark);
        m_out->SetCaretLineVisible(false);
        s->Add(m_out, 1, wxEXPAND);
        SetSizer(s);

        m_proc = new wxProcess(this);
        m_proc->Redirect();
        wxExecuteEnv env; env.cwd = cwd;
        wxGetEnvMap(&env.env);
#ifdef __WXMSW__
        env.env["CHERE_INVOKING"] = "1";   // Cygwin --login shells keep cwd instead of jumping to ~
#endif
        // wxEXEC_MAKE_GROUP_LEADER (Unix only - a no-op flag on Windows): without it the shell stays in
        // OUR process group, so shutdown()'s wxKILL_CHILDREN group-kill targets a group that doesn't
        // exist and silently kills nothing - not even the shell itself, let alone any children it
        // spawned (e.g. `npm run dev`), which would then survive the tab/app closing as orphans.
        m_pid = wxExecute(shell.cmd, wxEXEC_ASYNC | wxEXEC_MAKE_GROUP_LEADER, m_proc, &env);
        if (m_pid <= 0)
        {
            append(wxString::Format(_("Failed to start: %s"), shell.cmd) + "\n");
            delete m_proc; m_proc = nullptr;
        }
        else
        {
            m_poll.SetOwner(this);
            Bind(wxEVT_TIMER, [this](wxTimerEvent&){ drain(); });
            Bind(wxEVT_END_PROCESS, &TerminalTab::onExit, this);
            m_poll.Start(60);
        }
        m_out->Bind(wxEVT_KEY_DOWN, &TerminalTab::onKey, this);
    }
    ~TerminalTab() override { shutdown(); }

    // Kill the child + stop polling. Safe to call repeatedly; also used by the frame's exit teardown so
    // no child shell outlives the app and no timer can fire into a dying window.
    void shutdown()
    {
        m_poll.Stop();
        if (m_proc)
        {
            m_proc->Detach();   // self-deletes; no END_PROCESS into a dead window
            m_proc = nullptr;
            if (m_pid > 0)
            {
                // wxKill(..., wxKILL_CHILDREN) is a genuinely blocking call - on MSW it does
                // TerminateProcess + a 500ms WaitForSingleObject PER process in the tree (recursively,
                // once per descendant). Calling it inline here would freeze the UI for up to 500ms per
                // live process when closing a tab (or longer, serially, for every tab on app exit).
                // wxKill touches only plain OS process APIs (no wx GUI/event-loop state), so it's safe
                // to fire on a short-lived detached thread instead of blocking the caller.
                const long pid = m_pid;
                std::thread([pid] { wxKill(pid, wxSIGKILL, nullptr, wxKILL_CHILDREN); }).detach();
            }
            m_pid = 0;
        }
    }
    void focusInput() { m_out->SetFocus(); m_out->GotoPos(m_out->GetLength()); }

    // Re-colour for a light/dark switch (the toolbar's "lights" toggle). Re-applies the styles in
    // place rather than rebuilding the control, so the scrollback and the running shell survive it.
    void applyTheme(bool dark)
    {
        m_out->StyleSetBackground(wxSTC_STYLE_DEFAULT, dark ? wxColour(24, 24, 24) : wxColour(252, 252, 252));
        m_out->StyleSetForeground(wxSTC_STYLE_DEFAULT, dark ? wxColour(220, 220, 220) : wxColour(30, 30, 30));
        m_out->StyleClearAll();   // propagate STYLE_DEFAULT to every style; safe to re-run
        m_out->SetCaretForeground(dark ? wxColour(220, 220, 220) : wxColour(30, 30, 30));
        m_out->Refresh();
    }

private:
    // Bytes -> wxString with a carry buffer, so a multibyte sequence split across reads isn't dropped
    // (wx conversions reject a chunk wholesale on any invalid byte, not just from that point on - see
    // wxMBConvStrictUTF8::ToWChar/wxMBConv_win32::MB2WC, both single-pass and abort-on-first-error).
    // Two distinct failure shapes get handled: (a) a multibyte sequence truncated at the very END of
    // this read (the common case - the next drain's bytes complete it) - recovered by backing off up
    // to 3 bytes (UTF-8's max continuation length) and carrying them forward; (b) a genuinely invalid
    // byte somewhere in the MIDDLE of the buffer (a legacy tool writing raw OEM/ANSI bytes despite an
    // active UTF-8 console codepage, or similar) - recovered by resyncing past just that one byte and
    // continuing to decode the rest, instead of discarding every byte accumulated since the last
    // successful decode (which the old single-shot tail-trim-only version did).
    wxString convertChunk(wxMemoryBuffer& carry)
    {
#ifdef __WXMSW__
        wxCSConv oemConv(wxString::Format("CP%u", ::GetOEMCP()));
        wxMBConv& conv = m_oem ? static_cast<wxMBConv&>(oemConv) : static_cast<wxMBConv&>(wxConvUTF8);
#else
        wxMBConv& conv = wxConvUTF8;
#endif
        const char* data = static_cast<const char*>(carry.GetData());
        const size_t len = carry.GetDataLen();
        if (!len) return wxString();
        wxString out;
        size_t start = 0;
        while (start < len)
        {
            bool consumedAll = false;
            for (size_t cut = 0; cut <= 3 && cut < len - start; ++cut)
            {
                const size_t span = len - start - cut;
                wxString s(data + start, conv, span);
                if (!s.empty() || span == 0)
                {
                    out += s;
                    if (cut > 0)   // truncated tail (span+cut==len always) - carry it for the next drain
                    {
                        wxMemoryBuffer rest; rest.AppendData(data + start + span, cut);
                        carry = rest;
                        return out;
                    }
                    start = len; consumedAll = true;
                    break;
                }
            }
            if (consumedAll) break;
            ++start;   // no cut (0..3) decoded from `start` - one genuinely bad byte; skip and resync
        }
        carry.Clear();
        return out;
    }
    void slurp(wxInputStream* in, wxMemoryBuffer& carry)
    {
        char buf[4096];
        while (in && in->CanRead())
        {
            in->Read(buf, sizeof(buf));
            const size_t n = in->LastRead();
            if (!n) break;
            carry.AppendData(buf, n);
        }
    }
    void drain()
    {
        if (!m_proc) return;
        bool got = false;
        if (m_proc->IsInputAvailable()) { slurp(m_proc->GetInputStream(), m_outCarry); got = true; }
        if (m_proc->IsErrorAvailable()) { slurp(m_proc->GetErrorStream(), m_errCarry); got = true; }
        if (!got) return;
        wxString t = convertChunk(m_outCarry) + convertChunk(m_errCarry);
        if (!t.empty()) append(sanitize(t));
    }
    // Strip ANSI/VT control sequences (colours, cursor moves, titles) - v1 renders plain text - and
    // normalise line endings; progress-bar style bare CRs become newlines (ugly but readable).
    static wxString sanitize(wxString t)
    {
        static wxRegEx csi("\x1b\\[[0-9;?]*[@A-Za-z`]", wxRE_EXTENDED);
        static wxRegEx osc("\x1b\\][^\x07\x1b]*(\x07|\x1b\\\\)", wxRE_EXTENDED);
        if (csi.IsValid()) csi.ReplaceAll(&t, "");
        if (osc.IsValid()) osc.ReplaceAll(&t, "");
        t.Replace("\x1b", ""); t.Replace("\x07", "");
        t.Replace("\r\n", "\n"); t.Replace("\r", "\n");
        return t;
    }
    // Async process output can arrive while the user has an unsubmitted, partially-typed command
    // sitting after m_inputMark (a background job's stdout, a delayed prompt re-print, straggling
    // output from a PREVIOUS command - this is a raw pipe console, not a PTY with its own line
    // discipline, so nothing serialises "your command" against "async output" for us). Appending
    // unconditionally at the document end would splice new text into the middle of what's typed and
    // truncate what actually gets sent on the next Enter - so splice incoming output in BEFORE the
    // pending input instead, preserving both its content and the caret's position within it.
    void append(const wxString& t_)
    {
        // Drop the blank line a fresh shell leads with. cmd.exe's startup bytes are literally
        // "\r\n<cwd>>" - the gap it would normally leave under its banner - but there is no banner here,
        // so it just reads as a stray empty first line above the prompt. Only while the buffer is still
        // empty, so real blank lines in a command's output are untouched; and the copy the trim needs is
        // confined to that one call rather than taken on every drain for the rest of the session.
        wxString trimmed;
        const bool trim = (m_out->GetLength() == 0 && !t_.empty() && t_[0] == '\n');
        if (trim)
        {
            trimmed = t_;
            while (!trimmed.empty() && trimmed[0] == '\n') trimmed.Remove(0, 1);
            if (trimmed.empty()) return;
        }
        const wxString& t = trim ? trimmed : t_;
        if (m_out->GetLength() > m_inputMark)
        {
            const int caretOffset = m_out->GetCurrentPos() - m_inputMark;
            const int before = m_out->GetLength();
            m_out->InsertText(m_inputMark, t);
            m_inputMark += m_out->GetLength() - before;   // actual bytes Scintilla added, not wxString::length() (UTF-8 vs char count)
            m_out->GotoPos(m_inputMark + wxMax(caretOffset, 0));
        }
        else
        {
            m_out->AppendText(t);
            m_out->GotoPos(m_out->GetLength());
            m_inputMark = m_out->GetLength();
        }
    }
    void onKey(wxKeyEvent& e)
    {
        const int code = e.GetKeyCode();
        // On MSW, AltGr is delivered to wxEVT_KEY_DOWN as a phantom Ctrl+Alt chord (confirmed in wx's
        // own src/msw/window.cpp: CreateCharEvent strips this for wxEVT_CHAR, but CreateKeyEvent does
        // NOT for wxEVT_KEY_DOWN, by design - "the KEY_DOWN event would still have the correct [i.e.
        // uncorrected] modifiers if they're really needed"). Without this, every AltGr-composed key
        // (e.g. Polish ą/ć/ł/ę/ó/ś/ż/ź/ń) is misclassified as a non-editing keystroke below, so a
        // click into scrollback followed by typing an accented character never hops the caret back to
        // the input line first - it gets inserted into old output instead, and silently drops from
        // what's eventually sent on Enter. Treat Ctrl+Alt together as "no modifiers" here, mirroring
        // wx's own CreateCharEvent logic, so this printable-range check isn't fooled by it.
        const bool altGr = e.ControlDown() && e.AltDown();
        const bool noMods = altGr || (!e.ControlDown() && !e.AltDown());
        // Typing/erasing before the input mark would edit past OUTPUT - hop to the end first, like real
        // consoles. Plain navigation (arrows/PgUp/copy) stays free for scrollback reading.
        const bool editing = (code == WXK_BACK || code == WXK_DELETE ||
                              (code >= 32 && code < WXK_START && noMods));
        if (editing && m_out->GetCurrentPos() < m_inputMark) m_out->GotoPos(m_out->GetLength());
        if (code == WXK_BACK && m_out->GetCurrentPos() <= m_inputMark) return;   // don't chew into output
        if (code == WXK_RETURN || code == WXK_NUMPAD_ENTER)
        {
            const wxString line = m_out->GetTextRange(m_inputMark, m_out->GetLength());
            m_out->AppendText("\n");
            m_inputMark = m_out->GetLength();
            if (m_proc && m_proc->GetOutputStream())
            {
                const wxString withNl = line + "\n";
#ifdef __WXMSW__
                // m_oem shells (PowerShell/pwsh) read their console INPUT in the OEM codepage too -
                // symmetric with convertChunk()'s OUTPUT-side handling above. Writing UTF-8 bytes to
                // their stdin unconditionally (the old behaviour) mismatches that decoder for any
                // non-ASCII character the user types (e.g. a Polish letter in a path), corrupting the
                // command actually received without any visible error.
                if (m_oem)
                {
                    wxCSConv oemConv(wxString::Format("CP%u", ::GetOEMCP()));
                    const wxScopedCharBuffer u = withNl.mb_str(oemConv);
                    m_proc->GetOutputStream()->Write(u.data(), u.length());
                }
                else
#endif
                {
                    const wxScopedCharBuffer u = withNl.utf8_str();
                    m_proc->GetOutputStream()->Write(u.data(), u.length());
                }
            }
            return;
        }
        e.Skip();
    }
    void onExit(wxProcessEvent& ev)
    {
        m_poll.Stop();
        drain();
        append(wxString::Format(_("[process exited with code %d]"), ev.GetExitCode()) + "\n");
        delete m_proc; m_proc = nullptr; m_pid = 0;
    }

    wxStyledTextCtrl* m_out = nullptr;
    wxProcess*        m_proc = nullptr;
    long              m_pid = 0;
    wxTimer           m_poll;
    wxMemoryBuffer    m_outCarry, m_errCarry;
    int               m_inputMark = 0;
    bool              m_oem = false;
};

// The app's accent (Open-Color green 8) - the ONE definition; PinTabArt (main.cpp) marks the editor's
// active tab with this too, so the terminal chrome reads as part of wxNote rather than a bolted-on
// widget. Behind a function, not a namespace-scope object: a static wxColour would be constructed
// during static init, before wxWidgets is initialised.
inline const wxColour& termAccent() { static const wxColour c(47, 158, 68); return c; }

// Paint the active tab's top-edge marker. Shared by the terminal's tab strip and the editor's
// (PinTabArt::DrawPageTab), so the two never drift apart. Two traps here, each already paid for once:
//   - the base class's own marker sits at page.rect's left+1 / width-1, NOT at DrawPageTab's `extent`
//     return value (the advance to the NEXT tab, which can be narrower - sizing from it leaves a sliver
//     of system blue showing, a bug found and fixed once in PinTabArt);
//   - it is wnd->FromDIP(2) TALL, so a hardcoded 3px under-covers it at 175%+ scaling and leaves a row
//     of blue peeking out. FromDIP(3) is >= FromDIP(2) at every scale.
// The base releases its clipper before returning, so re-clip to the tab rect or the marker spills past
// the strip's edge when the tabs are scrolled.
inline void drawActiveTabMarker(wxDC& dc, wxWindow* wnd, const wxRect& tabRect, const wxRect& clipTo)
{
    wxDCClipper clip(dc, clipTo);
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(termAccent()));
    dc.DrawRectangle(tabRect.GetLeft() + 1, tabRect.GetTop(), tabRect.GetWidth() - 1, wnd->FromDIP(3));
}

// The UI (chrome) font - deliberately the system GUI font, NOT the monospace terminal font: button
// labels and the shell picker are chrome, and rendering them in Consolas looked like terminal output.
inline wxFont termUiFont() { return wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT); }

// How far the chrome's glyphs sit in from the panel's right edge, in DIP. ONE number, because the
// toolbar's buttons and the tab strip's sit in the same column and any difference between them reads
// as a misalignment. The toolbar reaches it as (sizer margin + half a button's padding); the tab strip
// as the art's inset - see TermFlatButton::DoGetBestSize and TermTabArt::btnGap.
static const int kTermEdge = 6;
static const int kTermIconPad = 8;   // icon-only button: total horizontal padding, so kTermIconPad/2 a side

// Chrome glyphs are VECTORS, never text. Drawing them as characters looks broken on Windows: Segoe UI
// (wxSYS_DEFAULT_GUI_FONT) has no U+25BE/U+25D0, so GDI font-links them down its SystemLink chain to
// MS UI Gothic, which serves both as 1-bit EMBEDDED BITMAPS - zero antialiasing, a 4x4 triangle and a
// 6x5 circle - jagged next to smooth ClearType text at every UI point size (raising the size only
// makes them bigger and still jagged). This is the same SVG -> wxBitmapBundle route the caption
// buttons take (see captionIcon in main.cpp); `inner` uses currentColor, swapped for the real colour
// here because the stroke is baked into the rasterised bitmap.
inline wxBitmapBundle termGlyph(const char* inner, const wxColour& col)
{
    wxString svg = wxString::Format(
        "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'>%s</svg>", inner);
    svg.Replace("currentColor", col.GetAsString(wxC2S_HTML_SYNTAX));
    const wxScopedCharBuffer u = svg.utf8_str();
    return wxBitmapBundle::FromSVG(u.data(), wxSize(16, 16));
}

#define WXN_TG_STROKE "fill='none' stroke='currentColor' stroke-width='1.6' stroke-linecap='round' stroke-linejoin='round'"
// The plus/caret/close reuse the caption buttons' own path data verbatim, so the terminal's toolbar
// glyphs are identical to the window's.
static const char* const kTGPlus   = "<path d='M8 4.5 V11.5 M4.5 8 H11.5' " WXN_TG_STROKE "/>";
static const char* const kTGCaret  = "<path d='M3.5 6.5 L8 11 L12.5 6.5' " WXN_TG_STROKE "/>";
static const char* const kTGClose  = "<path d='M4.5 4.5 L11.5 11.5 M11.5 4.5 L4.5 11.5' " WXN_TG_STROKE "/>";
// Hide the (bottom-docked) panel: a chevron collapsing DOWN onto a floor bar. Deliberately not a bare
// chevron - the shell chip's dropdown caret sits in the same bar and the two must not read alike - and
// deliberately not an X, which would suggest closing a tab rather than stowing the panel.
static const char* const kTGCollapse = "<path d='M4.5 5 L8 8.5 L11.5 5' " WXN_TG_STROKE "/>"
                                       "<path d='M4 12 H12' " WXN_TG_STROKE "/>";
// Half-filled circle - the conventional "contrast / switch theme" mark.
static const char* const kTGLights = "<circle cx='8' cy='8' r='5.25' " WXN_TG_STROKE "/>"
                                     "<path d='M8 2.75 A5.25 5.25 0 0 0 8 13.25 Z' fill='currentColor'/>";

// wx's own art marks the active tab with a wxSYS_COLOUR_HOTLIGHT bar - the SYSTEM accent, blue on
// Windows - whereas the editor's document tabs paint that same marker in the project's accent green
// (PinTabArt, main.cpp). Repaint it green here so the terminal's tabs match the document tabs above.
// Deliberately NOT reusing PinTabArt: that one also casts page.window to EditorPage for its per-tab
// colour wash, and these pages are TerminalTabs.
class TermTabArt : public wxAuiDefaultTabArt
{
public:
    // wxAuiFlatTabArt is non-copyable (private pimpl), so hand back a fresh instance rather than a copy
    // - the same reason PinTabArt::Clone does. No UpdateColoursFromSystem() call: the base constructor
    // has already run it, and repeating it rebuilds every button bitmap for nothing.
    wxAuiTabArt* Clone() override { return new TermTabArt(); }

    int DrawPageTab(wxDC& dc, wxWindow* wnd, wxAuiNotebookPage& page, const wxRect& rect) override
    {
        const int extent = wxAuiDefaultTabArt::DrawPageTab(dc, wnd, page, rect);
        if (page.active) drawActiveTabMarker(dc, wnd, page.rect, rect);
        return extent;
    }

    // The strip's right-hand buttons (the terminals list and the close X). Two fixes here:
    //  - SPACING. wx lays these out hard against the strip's right edge, and butted together. The caller
    //    hands the art a rect and the art right-aligns inside it, so narrowing it gives the outer
    //    margin; the gap BETWEEN them has to come out of each button's advance instead. Note wx uses
    //    two different sources for that advance - GetButtonRect()'s return when MEASURING, and
    //    DrawButton()'s outRect width when DRAWING - and tabart.h requires the two to agree, so both
    //    overrides below apply the identical adjustment. The gap is added on the LEFT (these lay out
    //    right-to-left), which also folds it into the button's hit rect.
    //  - the GLYPH. wx's own window-list mark is a filled triangle, sitting directly under the editor's
    //    identical "open documents" control - swap in the app's chevron so the two match.
    // DrawButtonBitmap() gets no bitmap id or window, so stash both on the way through.
    static int btnGap(wxWindow* wnd) { return wnd->FromDIP(kTermEdge); }

    int GetButtonRect(wxReadOnlyDC& dc, wxWindow* wnd, const wxRect& inRect, int bitmapId,
                      int buttonState, int orientation, wxRect* outRect = nullptr) override
    {
        wxRect r = inRect;
        if (orientation == wxRIGHT) r.width -= btnGap(wnd);
        wxRect out;
        const int w = wxAuiDefaultTabArt::GetButtonRect(dc, wnd, r, bitmapId, buttonState,
                                                        orientation, &out);
        if (orientation == wxRIGHT) { out.x -= btnGap(wnd); out.width += btnGap(wnd); }
        if (outRect) *outRect = out;
        return orientation == wxRIGHT ? w + btnGap(wnd) : w;
    }
    void DrawButton(wxDC& dc, wxWindow* wnd, const wxRect& inRect, int bitmapId, int buttonState,
                    int orientation, wxRect* outRect) override
    {
        m_btnId  = bitmapId;
        m_btnWnd = wnd;
        wxRect r = inRect;
        if (orientation == wxRIGHT) r.width -= btnGap(wnd);
        wxAuiDefaultTabArt::DrawButton(dc, wnd, r, bitmapId, buttonState, orientation, outRect);
        if (orientation == wxRIGHT && outRect) { outRect->x -= btnGap(wnd); outRect->width += btnGap(wnd); }
    }
    void DrawButtonBitmap(wxDC& dc, const wxRect& rect, const wxBitmap& bmp, int buttonState) override
    {
        if (m_btnId == wxAUI_BUTTON_WINDOWLIST && m_btnWnd)
        {
            // Built once, on the tab strip's own colours: this art has no access to the panel's
            // m_chromeDark, and the strip follows the system appearance regardless (see DrawPageTab).
            if (!m_caret.IsOk())
                m_caret = termGlyph(kTGCaret, wxSystemSettings::GetAppearance().IsDark()
                                                  ? wxColour(220, 220, 220) : wxColour(64, 64, 64));
            const wxBitmap g  = m_caret.GetBitmapFor(m_btnWnd);
            const wxSize   gs = g.GetLogicalSize();
            dc.DrawBitmap(g, rect.x + (rect.width - gs.x) / 2, rect.y + (rect.height - gs.y) / 2, true);
            return;   // replaces wx's triangle outright - no overdraw, nothing showing through
        }
        // Every other button, unchanged. The base's version is PRIVATE so it cannot be delegated to -
        // but its whole body is this one call, and the flat art does not override it.
        (void)buttonState;
        dc.DrawBitmap(bmp, rect.x, rect.y, true);
    }

private:
    wxBitmapBundle m_caret;
    wxWindow*      m_btnWnd = nullptr;
    int            m_btnId  = -1;
};

// A flat, owner-drawn toolbar button (the "+" and the "shell v" chip). wxButton with a custom colour
// falls back to the native themed button on MSW, which can't be flattened or given the app's green
// hover - so this draws itself: transparent normally, a rounded accent fill on hover. Not focusable,
// like a real toolbar button.
class TermFlatButton : public wxControl
{
public:
    // `glyph` is optional inner-SVG (see kTG* above); a button may carry a label, a glyph, or both.
    TermFlatButton(wxWindow* parent, const wxString& label, const char* glyph, const wxFont& font,
                   bool dark, std::function<void()> onClick)
        : wxControl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE),
          m_onClick(std::move(onClick))
    {
        m_bg = parent->GetBackgroundColour();
        m_fg = dark ? wxColour(210, 210, 210) : wxColour(45, 45, 45);
        if (glyph)   // rasterised twice: the stroke colour is baked in, and hover inverts to white-on-accent
        {
            m_glyph    = termGlyph(glyph, m_fg);
            m_glyphHot = termGlyph(glyph, *wxWHITE);
        }
        SetFont(font);
        SetLabel(label);
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetBackgroundColour(m_bg);
        Bind(wxEVT_PAINT,        &TermFlatButton::onPaint, this);
        Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent&){ m_hot = true;  Refresh(); });
        Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent&){ m_hot = false; m_down = false; Refresh(); });
        Bind(wxEVT_LEFT_DOWN,    [this](wxMouseEvent&){ m_down = true;  Refresh(); });
        // The second press of a double-click is delivered as LEFT_DCLICK, not LEFT_DOWN - without this
        // the second of two fast clicks on "+" is silently dropped.
        Bind(wxEVT_LEFT_DCLICK,  [this](wxMouseEvent&){ m_down = true;  Refresh(); });
        Bind(wxEVT_LEFT_UP,      [this](wxMouseEvent&){ const bool c = m_down; m_down = false; Refresh();
                                                        if (c && m_onClick) m_onClick(); });
        // AcceptsFocus() alone is not enough on wxGTK. wxControl's one-step ctor runs Create() from its
        // own body, so wxWindow's SetCanFocus(AcceptsFocus()) fires while the dynamic type is still
        // wxControl - it reaches wxWindowBase::AcceptsFocus() (true), never the override below, and GTK
        // ends up with can-focus set. Only wxGTK overrides SetCanFocus (the base is a no-op), which is
        // why MSW/macOS look right and hid this; the call is harmless on those ports.
        SetCanFocus(false);
    }
    void setText(const wxString& s) { SetLabel(s); InvalidateBestSize(); if (GetParent()) GetParent()->Layout(); Refresh(); }
    bool AcceptsFocus() const override { return false; }

    // Drop the hover/pressed paint. Needed when a dropdown opens off this button: wxGTK's popup grab
    // emits the LEAVE crossing with mode=GDK_CROSSING_GRAB, which wxGTK discards, so LEAVE_WINDOW never
    // arrives and the chip would stay latched hover-green. The generic ports also RE-POST the dismissing
    // click asynchronously, which can set m_down with no matching LEFT_UP behind it.
    void clearHover() { if (m_hot || m_down) { m_hot = false; m_down = false; Refresh(); } }

protected:
    wxSize DoGetBestSize() const override
    {
        wxClientDC dc(const_cast<TermFlatButton*>(this));
        dc.SetFont(GetFont());
        const wxString lbl = GetLabel();
        const wxSize t = lbl.empty() ? wxSize(0, 0) : dc.GetTextExtent(lbl);
        const wxSize g = m_glyph.IsOk() ? FromDIP(m_glyph.GetDefaultSize()) : wxSize(0, 0);
        const int gap = (t.x && g.x) ? FromDIP(4) : 0;
        // An icon-only button is padded tightly (kTermIconPad) so the right-hand cluster lines up with
        // the tab strip's buttons below it; a labelled one (the shell chip) keeps roomier padding so its
        // text isn't cramped against the hover pill's edge.
        const int pad = lbl.empty() ? FromDIP(kTermIconPad) : FromDIP(16);
        return wxSize(t.x + gap + g.x + pad, wxMax(t.y, g.y) + FromDIP(10));
    }

private:
    // Unbuffered: this draws a rect, a rounded rect, one string and one bitmap, and wxBG_STYLE_PAINT
    // means we already cover every pixel ourselves - there is nothing worth double-buffering.
    void onPaint(wxPaintEvent&)
    {
        wxPaintDC dc(this);
        const wxSize sz = GetClientSize();
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(m_bg);
        dc.DrawRectangle(0, 0, sz.x, sz.y);
        if (m_hot)
        {
            // Through a wxGraphicsContext, not the DC: on MSW the DC is GDI, which does not antialias at
            // all, so wxDC's rounded rectangle leaves visibly stair-stepped corners on the hover pill.
            // The GC (GDI+ here by default, Cairo on GTK, CoreGraphics on macOS) antialiases it. Scoped
            // so it dies before the label and glyph go back through the plain DC, keeping native
            // ClearType text rather than the GC's own rasteriser. Ordering is safe on GTK/macOS too,
            // where wxPaintDC IS a wxGCDC: Create(dc) wraps the SAME native context (a cairo_reference /
            // the same CGContextRef) with balanced save/restore, so the later DC drawing lands on top.
            std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
            if (gc)
            {
                gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);
                gc->SetPen(*wxTRANSPARENT_PEN);
                gc->SetBrush(m_down ? termAccent().ChangeLightness(88) : termAccent());
                gc->DrawRoundedRectangle(FromDIP(1), FromDIP(1),
                                         sz.x - FromDIP(2), sz.y - FromDIP(2), FromDIP(4));
            }
        }
        // Label then glyph, centred as one run, so the shell chip reads "cmd v" with a real vector caret.
        // GetBitmapFor(this) rasterises at THIS window's DPI scale - use it rather than picking a size by
        // hand. GetLogicalSize() then pairs with DoGetBestSize's FromDIP(): both are gated on
        // wxHAS_DPI_INDEPENDENT_PIXELS (set for GTK3/macOS, NOT for MSW), so measure and paint agree on
        // every port - GetWidth() would not.
        const wxBitmapBundle& bb = m_hot ? m_glyphHot : m_glyph;
        wxBitmap bmp = bb.IsOk() ? bb.GetBitmapFor(this) : wxBitmap();
        const wxSize gsz = bmp.IsOk() ? bmp.GetLogicalSize() : wxSize(0, 0);
        dc.SetFont(GetFont());
        dc.SetTextForeground(m_hot ? *wxWHITE : m_fg);
        const wxString lbl = GetLabel();
        const wxSize   t   = lbl.empty() ? wxSize(0, 0) : dc.GetTextExtent(lbl);
        const int gap = (t.x && gsz.x) ? FromDIP(4) : 0;
        int x = (sz.x - t.x - gap - gsz.x) / 2;
        if (t.x)        { dc.DrawText(lbl, x, (sz.y - t.y) / 2); x += t.x + gap; }
        if (bmp.IsOk()) { dc.DrawBitmap(bmp, x, (sz.y - gsz.y) / 2, true); }
    }

    std::function<void()> m_onClick;
    wxBitmapBundle m_glyph, m_glyphHot;   // normal + hover (white-on-accent) rasterisations
    wxColour m_bg, m_fg;
    bool     m_hot = false, m_down = false;
};

// The shell picker: a fully owner-drawn dark dropdown replacing the native wxChoice, whose popup list
// is drawn by the OS and stays bright-white on a dark panel (unfixable via SetBackgroundColour on MSW).
// Auto-dismisses on outside click (wxPopupTransientWindow); picking a shell both remembers it as the
// default and opens a new terminal of it, VS Code-style.
class TermListPopup : public wxPopupTransientWindow
{
public:
    std::function<void()> onClosed;   // fired when dismissed (outside click OR a pick), before self-destruction

    TermListPopup(wxWindow* parent, const std::vector<wxString>& items, int current, bool dark,
                  std::function<void(int)> onPick)
        : wxPopupTransientWindow(parent, wxBORDER_NONE),
          m_items(items), m_current(current), m_onPick(std::move(onPick))
    {
        m_bg     = dark ? wxColour(37, 37, 38)    : wxColour(250, 250, 250);
        m_fg     = dark ? wxColour(225, 225, 225) : wxColour(30, 30, 30);
        m_border = dark ? wxColour(70, 70, 72)    : wxColour(190, 190, 190);
        m_font   = termUiFont();
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        m_rowH = FromDIP(26);
        int w = FromDIP(130);
        { wxClientDC dc(this); dc.SetFont(m_font);
          for (const wxString& s : m_items) w = wxMax(w, dc.GetTextExtent(s).x + FromDIP(46)); }
        SetClientSize(w, (int)m_items.size() * m_rowH + FromDIP(2));
        Bind(wxEVT_PAINT,        &TermListPopup::onPaint,  this);
        Bind(wxEVT_MOTION,       &TermListPopup::onMotion, this);
        Bind(wxEVT_LEFT_UP,      &TermListPopup::onClick,  this);
        Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent&){ if (m_hot != -1) { m_hot = -1; Refresh(); } });
    }
    // Drop the list under `anchor`. Right-aligned for anchors living on the toolbar's right edge, so a
    // wide list grows leftwards into the panel instead of hanging off it.
    // Drop the list at a screen point - for a trigger that isn't a window of ours (wx's own tab-strip
    // window-list button), so there is nothing to anchor under.
    void showAt(const wxPoint& screenPt) { Position(screenPt, wxSize(0, 0)); Popup(); }

    // Close it ourselves, running the full dismissal (onClosed + the deferred self-destroy). The owner
    // toggles the list with this rather than trusting wx to auto-dismiss on the re-click: a real-click
    // UI test showed that dismissal does NOT reliably fire, so a chip click would stack a second popup
    // instead of closing the first. DismissAndNotify() is protected in the base but callable on self.
    void closeNow() { DismissAndNotify(); }

    void showBelow(wxWindow* anchor, bool alignRight = false)
    {
        wxPoint origin = anchor->GetScreenPosition();
        if (alignRight) origin.x += anchor->GetSize().x - GetSize().x;
        // Position(ptOrigin, size) treats `size` as a box to sit CLEAR of, offsetting by size.x as well
        // as size.y - handing it the anchor's full size drops the list diagonally down-RIGHT of the
        // button instead of under it. Only the vertical drop is wanted.
        Position(origin, wxSize(0, anchor->GetSize().y));
        Popup();
    }

protected:
    // Self-destroy once dismissed so each open doesn't leak a window; deferred past the current event so
    // the pick callback, which runs right after the dismissal, still has a live `this`. Guarded: wx does
    // not promise OnDismiss fires only once, and a second Destroy() would trip wxPendingDelete's
    // already-a-member wxCHECK.
    void OnDismiss() override
    {
        if (m_dismissed) return;
        m_dismissed = true;
        if (onClosed) onClosed();
        CallAfter([this]{ Destroy(); });
    }

private:
    // Rows start FromDIP(1) down (the border). Integer division truncates TOWARD ZERO, so a click in the
    // 1px band above the first row would divide to 0 and select row 0 - reject it explicitly.
    int rowAt(int y) const
    {
        const int rel = y - FromDIP(1);
        if (rel < 0) return -1;
        const int r = rel / m_rowH;
        return (r < (int)m_items.size()) ? r : -1;
    }
    void onMotion(wxMouseEvent& e) { const int r = rowAt(e.GetY()); if (r != m_hot) { m_hot = r; Refresh(); } }
    void onClick(wxMouseEvent& e)
    {
        const int r = rowAt(e.GetY());
        if (r < 0) return;
        auto cb = m_onPick; const int idx = r;
        // DismissAndNotify(), NOT Dismiss(): by wx's contract Dismiss() only hides and never calls
        // OnDismiss(), so a plain Dismiss() here would skip both onClosed (leaving the owner holding a
        // stale popup pointer) and the deferred Destroy() - leaking a window on every pick.
        DismissAndNotify();
        if (cb) cb(idx);
    }
    void onPaint(wxPaintEvent&)   // unbuffered, for the same DPI reason as TermFlatButton::onPaint
    {
        wxPaintDC dc(this);
        const wxSize sz = GetClientSize();
        dc.SetPen(m_border);
        dc.SetBrush(m_bg);
        dc.DrawRectangle(0, 0, sz.x, sz.y);
        dc.SetFont(m_font);
        for (int i = 0; i < (int)m_items.size(); ++i)
        {
            const wxRect r(FromDIP(1), FromDIP(1) + i * m_rowH, sz.x - FromDIP(2), m_rowH);
            const bool hot = (i == m_hot);
            if (hot) { dc.SetPen(*wxTRANSPARENT_PEN); dc.SetBrush(termAccent()); dc.DrawRectangle(r); }
            dc.SetTextForeground(hot ? *wxWHITE : m_fg);
            const int ty = r.y + (m_rowH - dc.GetCharHeight()) / 2;
            if (i == m_current) dc.DrawText(wxString::FromUTF8("\xE2\x80\xA2"), r.x + FromDIP(8), ty);   // bullet marks the current entry
            dc.DrawText(m_items[i], r.x + FromDIP(22), ty);
        }
    }

    std::vector<wxString>    m_items;
    int                      m_current = 0;
    std::function<void(int)> m_onPick;
    wxColour m_bg, m_fg, m_border;
    wxFont   m_font;
    int      m_rowH = 26;
    int      m_hot  = -1;
    bool     m_dismissed = false;   // OnDismiss is not promised to fire only once
};

class TerminalPanel : public wxPanel
{
public:
    std::function<wxString()> cwdProvider;      // frame supplies "the active document's directory"
    std::function<void()>     onCloseRequested; // the toolbar's X - the frame hides the pane (there is no
                                                // caption bar to close from any more)

    TerminalPanel(wxWindow* parent, bool dark, const wxString& fontFace)
        : wxPanel(parent), m_chromeDark(dark), m_termDark(dark), m_fontFace(fontFace),
          m_shells(detectTermShells())
    {
        SetBackgroundColour(dark ? wxColour(37, 37, 38) : wxColour(238, 238, 238));

        // One compact chrome bar; the wxAui pane caption is switched off (see toggleTerminal), so this
        // row is the panel's only header and carries its close button too. Every control is owner-drawn
        // so it matches the panel on each OS - the old native wxButton + wxChoice rendered as bright
        // system widgets, and a native combo's popup can't be dark-themed on MSW at all.
        //   [ + ] [ shell v ] ................... [ v ] [ lights ] [ x ]
        auto* top = new wxBoxSizer(wxHORIZONTAL);
        auto* newBtn = new TermFlatButton(this, "", kTGPlus, termUiFont(), dark, [this]{ addTerminal(m_sel); });
        newBtn->SetToolTip(_("New terminal"));
        m_shellBtn = new TermFlatButton(this, shellLabel(), kTGCaret, termUiFont(), dark, [this]{ openPicker(); });
        m_shellBtn->SetToolTip(_("Choose shell"));
        auto* lightsBtn = new TermFlatButton(this, "", kTGLights, termUiFont(), dark,
                                             [this]{ setTerminalDark(!m_termDark); });
        lightsBtn->SetToolTip(_("Switch terminal theme"));
        auto* closeBtn = new TermFlatButton(this, "", kTGCollapse, termUiFont(), dark,
                                            [this]{ if (onCloseRequested) onCloseRequested(); });
        closeBtn->SetToolTip(_("Hide terminal panel"));
        top->Add(newBtn,      0, wxALIGN_CENTRE_VERTICAL | wxLEFT, FromDIP(4));
        top->Add(m_shellBtn,  0, wxALIGN_CENTRE_VERTICAL | wxLEFT, FromDIP(1));
        top->AddStretchSpacer(1);
        top->Add(lightsBtn,   0, wxALIGN_CENTRE_VERTICAL);
        // The glyph sits kTermIconPad/2 inside its button, so the sizer only owes the remainder to land
        // it kTermEdge from the panel edge - matching the tab strip's buttons directly below.
        top->Add(closeBtn,    0, wxALIGN_CENTRE_VERTICAL | wxRIGHT, FromDIP(kTermEdge - kTermIconPad / 2));

        // The tab strip is left to wx's own art provider. An earlier attempt to darken it via
        // SetColour()/SetActiveColour() backfired: wx 3.3's default art is wxAuiFlatTabArt, where those
        // setters assign the tab TEXT colours (m_fgNormal/m_fgActive) - not the background, as they do in
        // wxAuiGenericTabArt - so they painted the active tab's label in the background colour and made
        // it vanish. UpdateColoursFromSystem() would have reset them on any theme change regardless.
        // Both right-hand tab-strip buttons come from wx itself, which adds WINDOWLIST before CLOSE, so
        // they land as [v][x] - the terminals list left of the close, matching the editor's tab strip.
        // CLOSE_BUTTON is a distinct bit from CLOSE_ON_ALL_TABS, so the per-tab x stays too. Neither
        // needs wiring: wx falls back to the ACTIVE page for a right-hand close and raises the ordinary
        // PAGE_CLOSE, which the handler below turns into a shutdown() of that tab's shell.
        m_nb = new wxAuiNotebook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                 wxAUI_NB_TOP | wxAUI_NB_CLOSE_ON_ALL_TABS | wxAUI_NB_CLOSE_BUTTON |
                                 wxAUI_NB_WINDOWLIST_BUTTON | wxAUI_NB_SCROLL_BUTTONS);
        // ...the window-list button DOES need intercepting: wx's built-in handler pops a native wxMenu,
        // the exact bright, un-themeable popup this chrome exists to avoid. A dynamic Bind runs before
        // wxAuiNotebook's own table entry, so not skipping keeps that handler from ever seeing it.
        m_nb->Bind(wxEVT_AUINOTEBOOK_BUTTON, [this](wxAuiNotebookEvent& ev){
            if (ev.GetInt() == wxAUI_BUTTON_WINDOWLIST) openTabList();
            else                                        ev.Skip();
        });
        m_nb->SetArtProvider(new TermTabArt());   // green active-tab marker, matching the document tabs

        // 1px rule between the toolbar and the tab strip - a small cue that the chrome is deliberate.
        auto* sep = new wxWindow(this, wxID_ANY, wxDefaultPosition, wxSize(-1, FromDIP(1)));
        sep->SetBackgroundColour(dark ? wxColour(60, 60, 62) : wxColour(205, 205, 205));

        auto* s = new wxBoxSizer(wxVERTICAL);
        s->Add(top, 0, wxEXPAND | wxTOP | wxBOTTOM, FromDIP(2));
        s->Add(sep, 0, wxEXPAND);
        s->Add(m_nb, 1, wxEXPAND);
        SetSizer(s);
        // Kill the child BEFORE the page window is destroyed (the tab's dtor would too, but doing it in
        // the close handler keeps the ordering explicit).
        m_nb->Bind(wxEVT_AUINOTEBOOK_PAGE_CLOSE, [this](wxAuiNotebookEvent& ev){
            if (auto* t = dynamic_cast<TerminalTab*>(m_nb->GetPage(ev.GetSelection()))) t->shutdown();
            ev.Skip();
        });
    }

    void addTerminal(int shellIdx)
    {
        if (m_shells.empty()) return;
        if (shellIdx < 0 || shellIdx >= (int)m_shells.size()) shellIdx = wxMax(m_sel, 0);
        const TermShell& sh = m_shells[(size_t)shellIdx];
        const wxString cwd = cwdProvider ? cwdProvider() : wxGetCwd();
        auto* tab = new TerminalTab(m_nb, sh, cwd, m_termDark, m_fontFace);   // new terminals join the current lights setting
        m_nb->AddPage(tab, wxString::Format("%s %d", sh.name, ++m_counter), true);
        installTabTips();   // wx creates its tab ctrls lazily - the first exists only once a page does
        tab->focusInput();
    }

    // The shell chip's caption is just the shell name - the dropdown caret beside it is the button's
    // vector glyph (kTGCaret), not a text character.
    wxString shellLabel() const
    {
        return (m_sel >= 0 && m_sel < (int)m_shells.size()) ? m_shells[m_sel].name : wxString("shell");
    }

    // The one entry point for both dropdowns. Toggles deterministically instead of trusting wx to
    // auto-dismiss the open list on the re-click:
    //   - a list of THIS anchor is already up  -> close it, done (toggle off);
    //   - a list of the OTHER anchor is up      -> close it, then open ours in its place;
    //   - wx DID already auto-dismiss this same click (it sometimes does - MSWDismissUnfocusedPopup and
    //     the generic re-post) -> the timestamp guard swallows the reopen the fall-through click causes.
    // m_livePopup is the truth for "is a list showing"; the guard only covers the wx-dismissed-first
    // race. hoverBtn (may be null for the tab strip's art button) gets its hover cleared on close.
    bool listReopenGuard(const void* anchorId) const
    {
        return m_popupAnchor == anchorId && (wxGetUTCTimeMillis() - m_popupClosedAt).GetValue() < 250;
    }
    void openList(TermFlatButton* hoverBtn, wxWindow* parent, const void* anchorId,
                  const std::vector<wxString>& items, int current,
                  std::function<void(int)> onPick, std::function<void(TermListPopup*)> show)
    {
        if (m_livePopup)
        {
            const bool sameAnchor = (m_liveAnchor == anchorId);
            m_livePopup->closeNow();   // runs onClosed synchronously -> m_livePopup = nullptr
            if (sameAnchor) return;
        }
        if (listReopenGuard(anchorId)) return;

        auto* pop = new TermListPopup(parent, items, current, m_chromeDark, std::move(onPick));   // a dropdown is chrome
        pop->onClosed = [this, anchorId, hoverBtn]{
            if (hoverBtn) hoverBtn->clearHover();
            m_livePopup     = nullptr;
            m_popupAnchor   = anchorId;
            m_popupClosedAt = wxGetUTCTimeMillis();
        };
        m_livePopup  = pop;
        m_liveAnchor = anchorId;
        show(pop);
    }

    // Picking a shell remembers it as the default the "+" button uses AND opens a new terminal of it
    // (VS Code-style: pick a profile -> get that terminal).
    void openPicker()
    {
        if (m_shells.empty() || !m_shellBtn) return;
        std::vector<wxString> names;
        for (const TermShell& s : m_shells) names.push_back(s.name);
        openList(m_shellBtn, m_shellBtn, m_shellBtn, names, m_sel,
            [this](int i){
                if (i < 0 || i >= (int)m_shells.size()) return;
                m_sel = i;
                if (m_shellBtn) m_shellBtn->setText(shellLabel());
                addTerminal(i);
            },
            [this](TermListPopup* p){ p->showBelow(m_shellBtn); });
    }

    // The tab strip's buttons are painted by the art, not real windows, so they cannot carry a tooltip
    // the way the toolbar's controls do. wxAuiTabCtrl's own motion handler tooltips TABS only and
    // UnsetToolTip()s anything else - buttons included - so hook the same event on it: Skip() first
    // (that handler owns the buttons' hover highlight, so swallowing the event would freeze it), then
    // apply ours via CallAfter so it lands AFTER wx's UnsetToolTip rather than being wiped by it.
    // Idempotent, and called after every AddPage: wx creates its tab ctrls lazily, so there is none to
    // hook at construction time.
    void installTabTips()
    {
        for (wxAuiTabCtrl* tc : m_nb->GetAllTabCtrls())
        {
            if (std::find(m_tipped.begin(), m_tipped.end(), tc) != m_tipped.end()) continue;
            m_tipped.push_back(tc);
            tc->Bind(wxEVT_MOTION, [tc](wxMouseEvent& e){
                e.Skip();
                const wxAuiTabContainerButton* b = tc->ButtonHitTest(e.GetPosition());
                wxString tip;
                if (b && b->id == wxAUI_BUTTON_WINDOWLIST) tip = _("Show open terminals");
                else if (b && b->id == wxAUI_BUTTON_CLOSE) tip = _("Close terminal");
                if (tip.empty()) return;   // not on a button: leave wx's own unset alone
                tc->CallAfter([tc, tip]{ if (tc->GetToolTipText() != tip) tc->SetToolTip(tip); });
            });
        }
    }

    // Jump to any open terminal by name. Driven by the notebook's own window-list button, so it anchors
    // at the pointer: that button belongs to wx's tab ctrl, not to us - there is no window of ours to
    // hang the list under.
    void openTabList()
    {
        if (!m_nb->GetPageCount()) return;
        std::vector<wxString> titles;
        for (size_t i = 0; i < m_nb->GetPageCount(); ++i) titles.push_back(m_nb->GetPageText(i));
        openList(nullptr, m_nb, m_nb, titles, m_nb->GetSelection(),
            [this](int i){ if (i >= 0 && i < (int)m_nb->GetPageCount()) { m_nb->SetSelection(i); focusActive(); } },
            [](TermListPopup* p){ p->showAt(wxGetMousePosition()); });
    }

    // The "lights" toggle: re-colours the TERMINAL ONLY - never the chrome. The toolbar, the separator
    // and the tab strip stay on the app's theme, so flipping the terminal to dark inside a light editor
    // doesn't drag a dark bar into an otherwise light window (and the tab strip, which wx themes from
    // the system, would mismatch it anyway). Restyles in place: the shells keep running.
    void setTerminalDark(bool dark)
    {
        m_termDark = dark;
        for (size_t i = 0; i < m_nb->GetPageCount(); ++i)
            if (auto* t = dynamic_cast<TerminalTab*>(m_nb->GetPage(i))) t->applyTheme(dark);
    }

    void focusActive()
    {
        if (auto* t = dynamic_cast<TerminalTab*>(m_nb->GetCurrentPage())) t->focusInput();
    }
    bool empty() const { return m_nb->GetPageCount() == 0; }
    void shutdownAll()
    {
        for (size_t i = 0; i < m_nb->GetPageCount(); ++i)
            if (auto* t = dynamic_cast<TerminalTab*>(m_nb->GetPage(i))) t->shutdown();
    }

private:
    bool                   m_chromeDark;   // the app's theme: toolbar, separator, popups - the "lights" button never touches it
    bool                   m_termDark;     // the terminal's own theme - all the "lights" button flips
    wxString               m_fontFace;
    std::vector<TermShell> m_shells;
    int                    m_sel = 0;        // index into m_shells: the shell the "+" button opens
    // Only the shell chip is kept - the rest of the toolbar's controls are owned by the sizer and never
    // referred to again once built.
    TermFlatButton*        m_shellBtn = nullptr;
    TermListPopup*         m_livePopup = nullptr;     // the dropdown currently showing, or null - the toggle's truth
    const void*            m_liveAnchor = nullptr;    // which anchor owns m_livePopup
    const void*            m_popupAnchor = nullptr;   // whose dropdown closed last (see listReopenGuard)...
    wxLongLong             m_popupClosedAt = 0;       // ...and when: together they suppress the reopen-on-same-click
    std::vector<wxAuiTabCtrl*> m_tipped;              // tab ctrls already hooked for button tooltips
    wxAuiNotebook*         m_nb = nullptr;
    int                    m_counter = 0;
};
