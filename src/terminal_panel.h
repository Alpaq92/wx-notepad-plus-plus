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
#elif defined(__WXMAC__)
    v.push_back({ "zsh", "/bin/zsh", false });
    if (wxFileExists("/bin/bash")) v.push_back({ "bash", "/bin/bash", false });
#else
    wxString sh; if (!wxGetEnv("SHELL", &sh) || sh.empty()) sh = "/bin/bash";
    v.push_back({ wxFileNameFromPath(sh), sh, false });
    if (sh != "/bin/bash" && wxFileExists("/bin/bash")) v.push_back({ "bash", "/bin/bash", false });
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
        m_out->StyleSetBackground(wxSTC_STYLE_DEFAULT, dark ? wxColour(24, 24, 24) : wxColour(252, 252, 252));
        m_out->StyleSetForeground(wxSTC_STYLE_DEFAULT, dark ? wxColour(220, 220, 220) : wxColour(30, 30, 30));
        m_out->StyleClearAll();
        m_out->SetCaretForeground(dark ? wxColour(220, 220, 220) : wxColour(30, 30, 30));
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
    void append(const wxString& t)
    {
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

class TerminalPanel : public wxPanel
{
public:
    std::function<wxString()> cwdProvider;   // frame supplies "the active document's directory"

    TerminalPanel(wxWindow* parent, bool dark, const wxString& fontFace)
        : wxPanel(parent), m_dark(dark), m_fontFace(fontFace), m_shells(detectTermShells())
    {
        SetBackgroundColour(dark ? wxColour(32, 32, 32) : wxColour(240, 240, 240));
        auto* top = new wxBoxSizer(wxHORIZONTAL);
        auto* add = new wxButton(this, wxID_ANY, "+", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT | wxBORDER_NONE);
        add->SetBackgroundColour(GetBackgroundColour());
        add->SetForegroundColour(dark ? wxColour(220, 220, 220) : wxColour(30, 30, 30));
        wxArrayString names; for (const TermShell& s : m_shells) names.Add(s.name);
        m_pick = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, names);
        if (!names.empty()) m_pick->SetSelection(0);
        top->Add(add, 0, wxALIGN_CENTRE_VERTICAL | wxLEFT | wxRIGHT, 4);
        top->Add(m_pick, 0, wxALIGN_CENTRE_VERTICAL | wxTOP | wxBOTTOM, 2);
        top->AddStretchSpacer(1);
        m_nb = new wxAuiNotebook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                 wxAUI_NB_TOP | wxAUI_NB_CLOSE_ON_ALL_TABS | wxAUI_NB_SCROLL_BUTTONS);
        auto* s = new wxBoxSizer(wxVERTICAL);
        s->Add(top, 0, wxEXPAND);
        s->Add(m_nb, 1, wxEXPAND);
        SetSizer(s);
        add->Bind(wxEVT_BUTTON, [this](wxCommandEvent&){ addTerminal(m_pick->GetSelection()); });
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
        if (shellIdx < 0 || shellIdx >= (int)m_shells.size()) shellIdx = m_pick ? wxMax(m_pick->GetSelection(), 0) : 0;
        const TermShell& sh = m_shells[(size_t)shellIdx];
        const wxString cwd = cwdProvider ? cwdProvider() : wxGetCwd();
        auto* tab = new TerminalTab(m_nb, sh, cwd, m_dark, m_fontFace);
        m_nb->AddPage(tab, wxString::Format("%s %d", sh.name, ++m_counter), true);
        tab->focusInput();
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
    bool                   m_dark;
    wxString               m_fontFace;
    std::vector<TermShell> m_shells;
    wxChoice*              m_pick = nullptr;
    wxAuiNotebook*         m_nb = nullptr;
    int                    m_counter = 0;
};
