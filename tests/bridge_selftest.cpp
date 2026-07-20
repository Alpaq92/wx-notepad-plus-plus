// SPDX-License-Identifier: Apache-2.0
//
// bridge_selftest - end-to-end behavioural self-test of the npp-bridge Phase-1 surface.
// Copyright 2026 The wxNote Authors.
//
// Boots the REAL application object (WxnApp + WxnShellFrame from src/main.cpp - compiled INTO this
// translation unit with wxIMPLEMENT_APP neutralized, so the genuine host pipeline runs: nib
// capability surface, plugin loader, save pipeline, wx command dispatcher), which loads the real
// npp_bridge plugin from <exe>/nib, which in turn loads the PROBE N++ plugin
// (packages/npp-bridge/example/example_plugin.cpp, built to <exe>/plugins/WxnProbe on Windows). The
// probe logs every notification (code + idFrom) plus its toolbar/allocator/dark-mode registration
// results to a JSON-lines file; this harness then drives open / edit / save / undo-to-savepoint /
// save-all / close through DIRECT host calls (the same file-scope nib seams the bridge itself uses -
// keymap_selftest style, never wxUIActionSimulator/OS input injection) and asserts the Phase-1
// acceptance points against the probe's log:
//
//   (a) NPPN_FILEBEFORESAVE precedes NPPN_FILESAVED, both carrying the SAME buffer id - including
//       for a BACKGROUND buffer written by Save All (the wrong-id regression the v2 events fix);
//   (b) NO false NPPN_FILESAVED after undo-to-savepoint (the savepoint-derived false positive);
//   (c) NPPM_ALLOCATE* grants land inside the host pools, disjoint from every host-reserved
//       marker/indicator/command number (asserted against the REAL constants in main.cpp);
//   (d) invoking an allocated command id round-trips host -> wx dispatcher -> bridge -> the probe's
//       messageProc (ids > 32767, so this also exercises the 16-bit WM_COMMAND wrap-safe path);
//   (e) NPPM_ISDARKMODEENABLED == the host's real dark state.
//
// Everything user-visible is sandboxed: a custom wxAppTraits redirects GetUserDataDir() into a
// scratch dir and a wxFileConfig replaces the registry config BEFORE WxnApp::OnInit runs, so the
// test can never read or clobber the real installation's session/recovery/preferences (nor hand off
// to a running wxnote via the reuse-instance IPC, which is config-gated off in the sandbox).
//
//   cmake --build build --target bridge_selftest && build/bin/bridge_selftest

#include <wx/app.h>
#include <wx/init.h>
#include <wx/apptrait.h>
#include <wx/stdpaths.h>
#include <wx/fileconf.h>
#include <wx/modalhook.h>   // headlessly auto-answer the confirmClose save prompt (Phase-4 shutdown-veto test)

// Pull the whole application into this TU with its app-entry macro neutralized: every wx header
// main.cpp includes is guard-deduplicated against the includes above, so this redefinition is the
// one that reaches line "wxIMPLEMENT_APP(WxnApp);". The selftest then subclasses WxnApp and supplies
// its own main() -> wxEntry() below - the documented embedding path.
#undef wxIMPLEMENT_APP
#define wxIMPLEMENT_APP(appname) /* neutralized: bridge_selftest provides its own app + main() */
#include "main.cpp"

// The N++ ABI numbers the log assertions speak (NPPN_*/SCN codes). Apache-2.0 clean-room header;
// main.cpp already included Scintilla.h from the same include set, so this adds only the NPPM/NPPN
// vocabulary.
#include "Notepad_plus_msgs.h"

#include <wx/filename.h>
#include <wx/file.h>
#include <wx/utils.h>
#include <filesystem>
#include <string>
#include <vector>
#include <cstdio>
#include <cstring>
#include <climits>

static int g_pass = 0;
static int g_failCount = 0;
static void check(bool ok, const char* what)
{
    std::printf(ok ? "  ok    %s\n" : "  FAIL  %s\n", what);
    std::fflush(stdout);   // flush per line so a hang's last-reached check is visible even when stdout is redirected (block-buffered)
    if (ok) ++g_pass; else ++g_failCount;
}

// ---- the sandbox (set in main() BEFORE wxEntry, read by the traits below) --------------------------
static wxString g_sandboxRoot;       // <temp>/wxnote_bridge_selftest
static wxString g_sandboxUserData;   // <root>/userdata - what the app believes its user-data dir is

// Headlessly answer the confirmClose "wxNote" save prompt (no OS input injection). The whole run has
// AskBeforeClose armed (so the Phase-4 shutdown VETO path can be driven), but every close in Phases 1-3
// wants the old discard-and-proceed behaviour, so the DEFAULT answer is Don't Save (wxID_NO -> discard,
// close proceeds). The shutdown test arms g_closeAnswer to wxID_CANCEL for one vetoed close, forcing
// confirmClose to return false. Any dialog that is NOT the "wxNote" prompt is shown as usual (wxID_NONE).
static int g_closeAnswer = wxID_NO;
class CloseDialogHook : public wxModalDialogHook
{
protected:
    int Enter(wxDialog* dlg) override
    { return (dlg && dlg->GetTitle() == "wxNote") ? g_closeAnswer : wxID_NONE; }
};

// wxStandardPaths whose GetUserDataDir() lands in the sandbox. Everything keyed off userDataDir() -
// session, recovery backups, keymap store, the POSIX plugins root - follows automatically, because
// main.cpp resolves it through wxStandardPaths::Get() on every call.
class SandboxStandardPaths : public wxStandardPaths
{
public:
    SandboxStandardPaths() = default;
    wxString GetUserDataDir() const override
    {
        return g_sandboxUserData.empty()
            ? wxFileName::GetTempDir() + wxFILE_SEP_PATH + "wxnote_bridge_selftest" + wxFILE_SEP_PATH + "userdata"
            : g_sandboxUserData;
    }
};
class SandboxTraits : public wxGUIAppTraits
{
public:
    wxStandardPaths& GetStandardPaths() override { return m_paths; }
private:
    SandboxStandardPaths m_paths;
};

// ---- probe-log access ------------------------------------------------------------------------------
// The probe writes <plugins-config-dir>/wxn_probe.jsonl: exeDir\plugins\Config on Windows (the same
// layout the bridge's loader scans), <user-data>/plugins/Config off-Windows (sandboxed above).
static wxString probeLogPathStr()
{
#ifdef __WXMSW__
    return wxPathOnly(wxStandardPaths::Get().GetExecutablePath())
         + "\\plugins\\Config\\wxn_probe.jsonl";
#else
    return g_sandboxUserData + "/plugins/Config/wxn_probe.jsonl";
#endif
}
// The scratch session file the probe's Phase-5 table writes (next to the log, in the plugins-config dir).
static wxString probeSessionPathStr()
{
#ifdef __WXMSW__
    return wxPathOnly(wxStandardPaths::Get().GetExecutablePath())
         + "\\plugins\\Config\\p5sess.xml";
#else
    return g_sandboxUserData + "/plugins/Config/p5sess.xml";
#endif
}
static std::vector<std::string> readLogLines()
{
    std::vector<std::string> out;
    wxFile f;
    {
        wxLogNull noLog;
        if (!f.Open(probeLogPathStr())) return out;
    }
    wxString all;
    f.ReadAll(&all, wxConvUTF8);
    const std::string s(all.utf8_str());
    size_t start = 0;
    while (start < s.size())
    {
        size_t nl = s.find('\n', start);
        if (nl == std::string::npos) nl = s.size();
        if (nl > start) out.push_back(s.substr(start, nl - start));
        start = nl + 1;
    }
    return out;
}
static int findFrom(const std::vector<std::string>& L, size_t from, const std::string& needle)
{
    for (size_t i = from; i < L.size(); ++i)
        if (L[i].find(needle) != std::string::npos) return static_cast<int>(i);
    return -1;
}
static int countFrom(const std::vector<std::string>& L, size_t from, const std::string& needle)
{
    int n = 0;
    for (size_t i = from; i < L.size(); ++i)
        if (L[i].find(needle) != std::string::npos) ++n;
    return n;
}
// The exact line the probe writes for a notification - the trailing '}' pins the id (no prefix match).
static std::string notifNeedle(unsigned code, unsigned long long idFrom)
{
    char b[96];
    std::snprintf(b, sizeof(b), "{\"k\":\"n\",\"c\":%u,\"i\":%llu}", code, idFrom);
    return b;
}
static std::string notifNeedleForPage(unsigned code, intptr_t pageId)
{
    return notifNeedle(code, static_cast<unsigned long long>(static_cast<uintptr_t>(pageId)));
}
// Same notification line, id-agnostic (prefix-only match) - for a notification whose triggering id is
// not known ahead of the search (e.g. a startup-time recovery restore whose fresh page id we never see).
static std::string notifNeedlePrefix(unsigned code)
{
    char b[64];
    std::snprintf(b, sizeof(b), "{\"k\":\"n\",\"c\":%u,\"i\":", code);
    return b;
}
// {"k":"<key>","ok":N,"first":N,"count":N} - the probe's allocator-result lines.
static bool parseAlloc(const std::vector<std::string>& L, const char* key,
                       long long& ok, int& first, int& count)
{
    char pre[64];
    std::snprintf(pre, sizeof(pre), "{\"k\":\"%s\",", key);
    const int i = findFrom(L, 0, pre);
    if (i < 0) return false;
    return std::sscanf(L[i].c_str(), "{\"k\":\"%*[^\"]\",\"ok\":%lld,\"first\":%d,\"count\":%d",
                       &ok, &first, &count) == 3;
}
static bool parseKV(const std::vector<std::string>& L, const char* prefix, long long& v)
{
    const int i = findFrom(L, 0, prefix);
    if (i < 0) return false;
    const size_t p = L[i].find(prefix);
    return std::sscanf(L[i].c_str() + p + std::strlen(prefix), "%lld", &v) == 1;
}
// The Phase-2 scripted table logs one {"k":"p2","m":<msg>,"r":<ret>,"s":"<str>"} line per message (the
// table runs exactly once - g_p2done guards it - so there is a single line per message in the whole log).
// p2ret returns the message's return value (LLONG_MIN if the line is missing/malformed); p2str returns
// the string it wrote back ("\x01" if missing).
static long long p2ret(const std::vector<std::string>& L, unsigned msg)
{
    char pre[48];
    std::snprintf(pre, sizeof(pre), "{\"k\":\"p2\",\"m\":%u,", msg);
    const int i = findFrom(L, 0, pre);
    if (i < 0) return LLONG_MIN;
    const size_t p = L[i].find("\"r\":");
    long long r = 0;
    if (p == std::string::npos || std::sscanf(L[i].c_str() + p + 4, "%lld", &r) != 1) return LLONG_MIN;
    return r;
}
static std::string p2str(const std::vector<std::string>& L, unsigned msg)
{
    char pre[48];
    std::snprintf(pre, sizeof(pre), "{\"k\":\"p2\",\"m\":%u,", msg);
    const int i = findFrom(L, 0, pre);
    if (i < 0) return "\x01";
    const size_t p = L[i].find("\"s\":\"");
    const size_t end = L[i].rfind("\"}");
    if (p == std::string::npos || end == std::string::npos || end < p + 5) return "\x01";
    return L[i].substr(p + 5, end - (p + 5));
}
// The Phase-3 table logs one {"k":"p3","t":"<tag>","v":<val>} line per probe. p3val returns the value
// for a tag (LLONG_MIN if the line is missing/malformed), searching from a mark so the parser sees only
// this run's lines.
static long long p3val(const std::vector<std::string>& L, const char* tag)
{
    char pre[64];
    std::snprintf(pre, sizeof(pre), "{\"k\":\"p3\",\"t\":\"%s\",", tag);
    const int i = findFrom(L, 0, pre);
    if (i < 0) return LLONG_MIN;
    const size_t p = L[i].find("\"v\":");
    long long v = 0;
    if (p == std::string::npos || std::sscanf(L[i].c_str() + p + 4, "%lld", &v) != 1) return LLONG_MIN;
    return v;
}
// The Phase-4 table logs {"k":"p4","t":"<tag>","v":<val>} lines (same shape as p3).
static long long p4val(const std::vector<std::string>& L, const char* tag)
{
    char pre[64];
    std::snprintf(pre, sizeof(pre), "{\"k\":\"p4\",\"t\":\"%s\",", tag);
    const int i = findFrom(L, 0, pre);
    if (i < 0) return LLONG_MIN;
    const size_t p = L[i].find("\"v\":");
    long long v = 0;
    if (p == std::string::npos || std::sscanf(L[i].c_str() + p + 4, "%lld", &v) != 1) return LLONG_MIN;
    return v;
}
// The Phase-5 table logs {"k":"p5","t":"<tag>","v":<val>} lines (same shape as p3/p4).
static long long p5val(const std::vector<std::string>& L, const char* tag)
{
    char pre[64];
    std::snprintf(pre, sizeof(pre), "{\"k\":\"p5\",\"t\":\"%s\",", tag);
    const int i = findFrom(L, 0, pre);
    if (i < 0) return LLONG_MIN;
    const size_t p = L[i].find("\"v\":");
    long long v = 0;
    if (p == std::string::npos || std::sscanf(L[i].c_str() + p + 4, "%lld", &v) != 1) return LLONG_MIN;
    return v;
}
// The probe logs one {"k":"gm","mt":<modificationType>} line per NPPN_GLOBALMODIFIED. countGm counts
// them (from a mark); gmOrFrom ORs their modificationType bitsets (the opted-in flags that came through).
static int countGm(const std::vector<std::string>& L, size_t from)
{
    int n = 0; unsigned mt = 0;
    for (size_t i = from; i < L.size(); ++i)
        if (std::sscanf(L[i].c_str(), "{\"k\":\"gm\",\"mt\":%u", &mt) == 1) ++n;
    return n;
}
static unsigned gmOrFrom(const std::vector<std::string>& L, size_t from)
{
    unsigned acc = 0, mt = 0;
    for (size_t i = from; i < L.size(); ++i)
        if (std::sscanf(L[i].c_str(), "{\"k\":\"gm\",\"mt\":%u", &mt) == 1) acc |= mt;
    return acc;
}

static void pump(int ms = 60)
{
    wxYield();
    if (ms > 0) wxMilliSleep(ms);
    wxYield();
}

static bool writeWholeFile(const wxString& path, const char* content)
{
    wxLogNull noLog;
    wxFile f(path, wxFile::write);
    return f.IsOpened() && f.Write(content, std::strlen(content)) == std::strlen(content);
}
static wxString readWholeFile(const wxString& path)
{
    wxLogNull noLog;
    wxString out;
    wxFile f(path, wxFile::read);
    if (f.IsOpened()) f.ReadAll(&out, wxConvUTF8);
    return out;
}

// ====================================================================================================
class BridgeSelfTestApp : public WxnApp
{
public:
    bool OnInit() override
    {
        // Sandbox the config BEFORE the base OnInit's first wxConfigBase::Get() touch (readUiLang):
        // with an explicit object Set(), wx never auto-creates the registry/user config, so
        // ReuseInstance/IntegratedBar/theme/session state all resolve from this scratch file - and
        // the reuse-instance IPC handoff to a genuinely running wxnote can never trigger.
        wxFileName::Mkdir(g_sandboxUserData, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
        wxConfigBase::Set(new wxFileConfig("wxNoteBridgeSelftest", wxEmptyString,
                                           g_sandboxUserData + wxFILE_SEP_PATH + "selftest.ini",
                                           wxEmptyString, wxCONFIG_USE_LOCAL_FILE));
        // Arm "Ask before closing unsaved changes" so the Phase-4 shutdown-VETO path (confirmClose ->
        // Cancel) can be driven headlessly; the CloseDialogHook auto-answers the resulting prompt (Don't
        // Save by default), so no close in Phases 1-3 hangs. Written before OnInit reads it (loadSettings).
        wxConfigBase::Get()->Write("AskBeforeClose", true);
        // Phase 6: seed one dirty-recovery backup BEFORE WxnApp::OnInit() runs restoreSession() ->
        // restoreRecoveryBackups() (main.cpp), so the SNAPSHOTDIRTYFILELOADED assertion in runAll()
        // observes a REAL startup recovery restore - same manifest-entry + ".bak" file shape
        // backupUnsavedChanges() writes on a real crash, just written directly instead of driving one.
        // Path left empty (untitled-style recovery), so restoreRecoveryBackups() opens it as a fresh tab.
        {
            const wxString recDir = g_sandboxUserData + wxFILE_SEP_PATH + "RecoveryBackups";
            wxFileName::Mkdir(recDir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
            wxFile bak(recDir + wxFILE_SEP_PATH + "seed1.bak", wxFile::write);
            if (bak.IsOpened())
            { const char content[] = "phase-6 seeded dirty recovery content\n"; bak.Write(content, sizeof(content) - 1); }
            wxConfigBase::Get()->Write("Recovery/seed1/Title", wxString("P6 Recovered"));
        }
        wxConfigBase::Get()->Flush();
        m_closeHook.Register();
        if (!WxnApp::OnInit()) return false;   // the REAL boot: frame + nib surface + loadNibPlugins()
        CallAfter([this] { runAll(); });       // run once the loop is live (CallAfter/event paths behave as in the app)
        return true;
    }
    wxAppTraits* CreateTraits() override { return new SandboxTraits; }

private:
    CloseDialogHook m_closeHook;   // registered in OnInit; auto-unregisters on destruction

    void runAll()
    {
        std::printf("bridge_selftest\n");

        // ---- boot phase: the probe reached us through host -> npp_bridge -> probe ------------------
        pump(120);   // let any boot-time stragglers land before the log snapshot
        std::vector<std::string> L = readLogLines();
        check(!L.empty(), "probe log exists (host loaded npp_bridge, bridge loaded the probe)");
        long long pid = -1;
        parseKV(L, "{\"k\":\"start\",\"pid\":", pid);
        check(pid == static_cast<long long>(wxGetProcessId()),
              "log pid-stamp matches this process (fresh log, not a stale run's)");

        const int iReady = findFrom(L, 0, notifNeedle(NPPN_READY, 0));
        const int iTb    = findFrom(L, 0, notifNeedle(NPPN_TBMODIFICATION, 0));
        check(iReady >= 0, "NPPN_READY delivered to the probe");
        check(iTb >= 0 && iTb > iReady, "NPPN_TBMODIFICATION delivered, after NPPN_READY");

        // ---- toolbar registration (probe ran it inside its TBMODIFICATION handler) ----------------
        long long tbOk = 0; int tbCmd = -1;
        {
            const int i = findFrom(L, 0, "{\"k\":\"tb\",");
            check(i >= 0 && std::sscanf(L[i].c_str(), "{\"k\":\"tb\",\"ok\":%lld,\"cmd\":%d", &tbOk, &tbCmd) == 2,
                  "probe logged its toolbar registration");
        }
        check(tbOk == TRUE, "NPPM_ADDTOOLBARICON_FORDARKMODE answered TRUE");
        check(tbCmd >= NIB_ALLOC_CMD_FIRST && tbCmd <= NIB_ALLOC_CMD_LAST,
              "the probe's FuncItem got a host-granted command id (bridge wrote it into _cmdID)");

        // ---- the registered button must actually carry a visible image on the host toolbar ---------
        // Registration answering TRUE is not the same as a button a user can see: the stored bundle
        // could rasterize blank (alpha lost somewhere in HBITMAP -> RGBA -> wxImage -> bundle) and this
        // suite would still have been all-green. Rasterize what the toolbar actually holds and demand
        // the glyph's pixels survived.
        {
            wxToolBar* tb = nullptr;
            if (auto* frame = wxDynamicCast(wxTheApp->GetTopWindow(), wxFrame))
            {
                tb = frame->GetToolBar();                      // classic mode: the frame's own toolbar
                if (!tb)                                       // integrated mode: an aui-docked child
                    for (wxWindow* w : frame->GetChildren())
                        if ((tb = wxDynamicCast(w, wxToolBar)) != nullptr) break;
            }
            check(tb != nullptr, "host toolbar located for the probe-button image check");
            wxToolBarToolBase* tool = tb ? tb->FindById(tbCmd & 0xFFFF) : nullptr;
            check(tool != nullptr, "probe toolbar button exists on the host toolbar (FindById)");
            int visible = 0, greenish = 0;
            if (tb && tool)
            {
                const wxBitmap bmp = tool->GetNormalBitmapBundle().GetBitmap(tb->GetToolBitmapSize());
                const wxImage img = bmp.ConvertToImage();
                for (int y = 0; y < img.GetHeight(); ++y)
                    for (int x = 0; x < img.GetWidth(); ++x)
                    {
                        if (img.HasAlpha() && img.GetAlpha(x, y) < 128) continue;
                        ++visible;
                        if (img.GetGreen(x, y) > img.GetRed(x, y) + 20) ++greenish;
                    }
            }
            check(visible >= 8, "probe button image is not blank (opaque pixels survived to the stored bundle)");
            check(greenish >= 4, "probe button image kept its glyph (green-dominant pixels present)");
        }

        // ---- (c) allocator grants: inside the pools, disjoint from every host-reserved number ------
        long long okC = 0, okM = 0, okI = 0; int cFirst = -1, mFirst = -1, iFirst = -1, n = 0;
        check(parseAlloc(L, "allocCmd", okC, cFirst, n) && okC == TRUE && n == 4,
              "NPPM_ALLOCATECMDID granted 4 ids (Phase 2/3/4/5 table triggers)");
        check(cFirst >= NIB_ALLOC_CMD_FIRST && cFirst + 3 <= NIB_ALLOC_CMD_LAST,
              "(c) cmd-id grant inside the host pool (64000..64999; clear of kCmd*/menu/NIB_CMD ids)");
        check(parseAlloc(L, "allocMark", okM, mFirst, n) && okM == TRUE && n == 1,
              "NPPM_ALLOCATEMARKER granted 1 marker");
        check(mFirst >= 3 && mFirst <= 20,
              "(c) marker grant inside the host pool (3..20)");
        check(mFirst != MARK_BOOKMARK && !(mFirst >= 21 && mFirst <= 31),
              "(c) marker grant disjoint from bookmark=2, change-history 21..24 and fold chrome 25..31");
        check(parseAlloc(L, "allocInd", okI, iFirst, n) && okI == TRUE && n == 1,
              "NPPM_ALLOCATEINDICATOR granted 1 indicator");
        check((iFirst >= 12 && iFirst <= 20) || (iFirst >= 26 && iFirst <= 31),
              "(c) indicator grant inside the host pools (12..20 / 26..31)");
        check(iFirst > 8 && iFirst != MARK_INDIC && iFirst != SMART_INDIC && iFirst != URL_INDIC
                  && !(iFirst >= MARK_STYLE_BASE && iFirst <= MARK_STYLE_BASE + 4),
              "(c) indicator grant disjoint from lexer 0..8, mark/smart/url 9..11 and mark-styles 21..25");

        // ---- (e) dark-mode probe == host state -----------------------------------------------------
        long long darkV = -1;
        check(parseKV(L, "{\"k\":\"dark\",\"v\":", darkV), "probe logged NPPM_ISDARKMODEENABLED");
        check(darkV == (g_nibUiIsDark ? g_nibUiIsDark() : 0),
              "(e) NPPM_ISDARKMODEENABLED matches the host's real dark state");

        // ---- close the boot-time recovery-restored tab before it can interfere with later doc-count
        // assumptions ---------------------------------------------------------------------------------
        // main() seeded a Recovery/seed1 entry BEFORE wxEntry() (see BridgeSelfTestApp::OnInit), so
        // WxnApp::OnInit()'s restoreSession() -> restoreRecoveryBackups() already restored it as the
        // sole open document (replacing the startup "new 1") before runAll() ever ran. It's dirty and
        // untitled by design - that's exactly what NPPN_SNAPSHOTDIRTYFILELOADED (asserted purely from
        // the log, in Phase 6 below) is about - but left open it is exactly the kind of background
        // dirty/untitled buffer onSaveAll() is supposed to sweep up, so Phase 1's Save-All test just
        // below would activate it and run onSaveAs() on it, popping a REAL blocking Save-As dialog.
        // Close it now (CloseDialogHook auto-answers Don't Save) so the rest of the run has a clean
        // baseline; closing it early doesn't weaken the Phase 6 check, which never re-opens the tab -
        // it just re-reads the boot-time log line.
        check(g_nibDocCount && g_nibDocCount() == 1,
              "sole open document after boot is the recovery-restored tab");
        g_nibInvokeCommand(kCmdFileClose);
        pump();
        check(g_nibDocCount && g_nibDocCount() == 1,
              "recovery tab closed (discarded) - back to a fresh, clean single document");

        // ---- fixture files in the sandbox ----------------------------------------------------------
        const wxString work = g_sandboxRoot + wxFILE_SEP_PATH + "work";
        wxFileName::Mkdir(work, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
        const wxString fileA = work + wxFILE_SEP_PATH + "a.txt";
        const wxString fileB = work + wxFILE_SEP_PATH + "b.txt";
        check(writeWholeFile(fileA, "alpha\n") && writeWholeFile(fileB, "bravo\n"),
              "fixture files created in the sandbox");

        // ---- Phase 2: scripted NPPM getter/stub table against a known fixture -----------------------
        // Open a fixture whose single line and caret are known, then trigger the probe's Phase-2 table
        // (it fires on the first allocated cmd id - see the probe's messageProc). The probe calls each
        // Phase-2 message through ::SendMessage and logs {"k":"p2",...}; here we assert the exact
        // returns/strings. This runs BEFORE the open/save/close flow so the fixture is isolated.
        {
            const wxString fileP2 = work + wxFILE_SEP_PATH + "p2.txt";
            // line: "hello wxNote/plugin.cpp bridge" - caret at index 9 sits inside the word "wxNote"
            // (a word boundary at '/') and inside the filename token "wxNote/plugin.cpp" ('/' and '.'
            // are filename chars). Byte offsets: h0 e1 l2 l3 o4 (sp)5 w6 x7 N8 o9 t10 e11 /12 ...
            check(writeWholeFile(fileP2, "hello wxNote/plugin.cpp bridge\n"), "p2 fixture created");
            const int p2BaseDocs = (g_nibDocCount ? g_nibDocCount() : 1);   // restore to this after the table
            g_nibDocOpen(std::string(fileP2.utf8_str()).c_str());
            pump();
            nibSciCall(nullptr, -1, SCI_GOTOPOS, 9, 0);        // caret inside "wxNote"
            pump();
            check(coreSciCall(-1, SCI_GETCURRENTPOS, 0, 0) == 9, "p2 caret placed at index 9");

            const size_t p2mark = readLogLines().size();
            g_nibInvokeCommand(cFirst);                        // fire the probe's Phase-2 table (one-shot)
            pump(120);
            std::vector<std::string> P = readLogLines();
            // discard everything before the trigger so p2ret/p2str see only this run's lines
            P.erase(P.begin(), P.begin() + std::min(p2mark, P.size()));

            // -- the 15 implemented getters --
            check(p2str(P, NPPM_GETCURRENTWORD) == "wxNote" && p2ret(P, NPPM_GETCURRENTWORD) == 6,
                  "NPPM_GETCURRENTWORD == \"wxNote\" (word at caret)");
            check(p2str(P, NPPM_GETCURRENTLINESTR) == "hello wxNote/plugin.cpp bridge"
                      && p2ret(P, NPPM_GETCURRENTLINESTR) == 30,
                  "NPPM_GETCURRENTLINESTR == the caret line, EOL stripped");
            check(p2str(P, NPPM_GETFILENAMEATCURSOR) == "wxNote/plugin.cpp"
                      && p2ret(P, NPPM_GETFILENAMEATCURSOR) == 17,
                  "NPPM_GETFILENAMEATCURSOR == \"wxNote/plugin.cpp\" (filename token at caret)");
            check(p2ret(P, NPPM_GETNPPFULLFILEPATH) > 0 && !p2str(P, NPPM_GETNPPFULLFILEPATH).empty(),
                  "NPPM_GETNPPFULLFILEPATH returns the exe's own full path (non-empty)");
            check(p2ret(P, NPPM_GETNPPSETTINGSDIRPATH) == TRUE
                      && p2str(P, NPPM_GETNPPSETTINGSDIRPATH).find("userdata") != std::string::npos,
                  "NPPM_GETNPPSETTINGSDIRPATH returns the host user-data dir");
            check(p2ret(P, NPPM_GETCURRENTNATIVELANGENCODING) == 65001,
                  "NPPM_GETCURRENTNATIVELANGENCODING == 65001 (UTF-8 code page)");
            check(p2str(P, NPPM_GETLANGUAGENAME) == "C++" && p2ret(P, NPPM_GETLANGUAGENAME) == 3,
                  "NPPM_GETLANGUAGENAME(L_CPP) == \"C++\"");
            check(p2str(P, NPPM_GETLANGUAGEDESC) == "C++ source file" && p2ret(P, NPPM_GETLANGUAGEDESC) == 15,
                  "NPPM_GETLANGUAGEDESC(L_CPP) == \"C++ source file\"");
            check(p2ret(P, NPPM_GETBOOKMARKID) == MARK_BOOKMARK,
                  "NPPM_GETBOOKMARKID == the host's bookmark marker number (2)");
            check(p2ret(P, NPPM_GETAPPDATAPLUGINSALLOWED) == TRUE,
                  "NPPM_GETAPPDATAPLUGINSALLOWED == TRUE (plugins load from the user-writable dir)");
            check(p2ret(P, NPPM_SETSMOOTHFONT) == TRUE, "NPPM_SETSMOOTHFONT answered TRUE");
            check(p2ret(P, NPPM_RELOADBUFFERID) == TRUE,
                  "NPPM_RELOADBUFFERID reloaded the fixture buffer by id");
#ifdef _WIN32
            check(p2ret(P, NPPM_GETWINDOWSVERSION) > 0,
                  "NPPM_GETWINDOWSVERSION returns a real winVer on Windows");
            check(p2ret(P, NPPM_GETCURRENTCMDLINE) > 0 && !p2str(P, NPPM_GETCURRENTCMDLINE).empty(),
                  "NPPM_GETCURRENTCMDLINE returns the process command line on Windows");
#else
            check(p2ret(P, NPPM_GETWINDOWSVERSION) == WV_UNKNOWN,
                  "NPPM_GETWINDOWSVERSION == WV_UNKNOWN off-Windows (honest, no fragile per-OS hack)");
            check(p2ret(P, NPPM_GETCURRENTCMDLINE) == 0,
                  "NPPM_GETCURRENTCMDLINE == empty off-Windows (documented: needs a host hook)");
#endif
            // -- the documented no-op / interim stubs: every message answers (zero silent drops) --
            struct { unsigned msg; long long want; const char* what; } stubs[] = {
                { NPPM_DESTROYSCINTILLAHANDLE_DEPRECATED,      TRUE,  "DESTROYSCINTILLAHANDLE_DEPRECATED -> TRUE (N++ no-ops it)" },
                { NPPM_ENCODESCI,                              1,     "ENCODESCI -> UTF-8 UniMode (buffers are always UTF-8)" },
                { NPPM_DECODESCI,                              1,     "DECODESCI -> UTF-8 UniMode" },
                { NPPM_GETENABLETHEMETEXTUREFUNC_DEPRECATED,   0,     "GETENABLETHEMETEXTUREFUNC_DEPRECATED -> 0" },
                { NPPM_DOCLISTDISABLEEXTCOLUMN,                TRUE,  "DOCLISTDISABLEEXTCOLUMN -> TRUE (no columns to disable)" },
                { NPPM_DOCLISTDISABLEPATHCOLUMN,               TRUE,  "DOCLISTDISABLEPATHCOLUMN -> TRUE" },
                { NPPM_DISABLEAUTOUPDATE,                      TRUE,  "DISABLEAUTOUPDATE -> TRUE (no updater)" },
                { NPPM_GETEXTERNALLEXERAUTOINDENTMODE,         FALSE, "GETEXTERNALLEXERAUTOINDENTMODE -> FALSE" },
                { NPPM_SETEXTERNALLEXERAUTOINDENTMODE,         FALSE, "SETEXTERNALLEXERAUTOINDENTMODE -> FALSE" },
                { NPPM_MODELESSDIALOG,                         0x5EED, "MODELESSDIALOG -> echoes the passed handle (Annex W portable no-op)" },
                { NPPM_DMMVIEWOTHERTAB,                        0,     "DMMVIEWOTHERTAB -> NULL (Annex W portable no-op)" },
                { NPPM_SETEDITORBORDEREDGE,                    FALSE, "SETEDITORBORDEREDGE -> FALSE (Annex W portable no-op)" },
                { NPPM_DMMGETPLUGINHWNDBYNAME,                 0,     "DMMGETPLUGINHWNDBYNAME -> NULL (Annex W portable no-op)" },
                { NPPM_CREATESCINTILLAHANDLE,                  0,     "CREATESCINTILLAHANDLE -> NULL" },
                { NPPM_HIDETABBAR,                             FALSE, "HIDETABBAR -> FALSE" },
                { NPPM_ISTABBARHIDDEN,                         FALSE, "ISTABBARHIDDEN -> FALSE" },
                { NPPM_DARKMODESUBCLASSANDTHEME,               0,     "DARKMODESUBCLASSANDTHEME -> 0" },
                { NPPM_REMOVESHORTCUTBYCMDID,                  FALSE, "REMOVESHORTCUTBYCMDID -> FALSE (Phase 5 documented no-op)" },
                { NPPM_TRIGGERTABBARCONTEXTMENU,               FALSE, "TRIGGERTABBARCONTEXTMENU -> FALSE (Phase 5 documented no-op)" },
            };
            bool allStubs = true;
            for (const auto& s : stubs)
                if (p2ret(P, s.msg) != s.want) { allStubs = false; check(false, s.what); }
            check(allStubs, "every documented no-op/interim stub returned its exact documented value");
            check(p2ret(P, NPPM_GETSETTINGSONCLOUDPATH) == 0 && p2str(P, NPPM_GETSETTINGSONCLOUDPATH).empty(),
                  "GETSETTINGSONCLOUDPATH -> empty string (no cloud settings)");

            // Clean up: RELOADBUFFERID re-opens the (already-open) fixture through the non-deduping
            // nib.documents open(), so the reload leaves a duplicate tab - exactly as the shipped
            // NPPM_RELOADFILE does (a switch-if-already-open seam is Phase 3 work). Close every buffer
            // the section added, back to the pre-Phase-2 baseline, so the doc-count flow below is
            // unperturbed. All of them are clean (the fixture was only reloaded), so no close prompts.
            for (int guard = 0; g_nibDocCount && g_nibDocCount() > p2BaseDocs && guard < 12; ++guard) {
                g_nibInvokeCommand(kCmdFileClose);
                pump();
            }
            check(g_nibDocCount && g_nibDocCount() == p2BaseDocs,
                  "p2 fixture(s) closed - doc model restored to the pre-Phase-2 baseline");
        }

        // ---- open: BEFOREOPEN precedes FILEOPENED with the same id ---------------------------------
        size_t mark = readLogLines().size();
        g_nibDocOpen(std::string(fileA.utf8_str()).c_str());   // DIRECT host call (the nib.documents seam)
        pump();
        const intptr_t idA = g_nibDocActiveId ? g_nibDocActiveId() : 0;
        check(idA != 0, "opened a.txt (active buffer id is non-zero)");
        L = readLogLines();
        {
            const int iBo = findFrom(L, mark, notifNeedleForPage(NPPN_FILEBEFOREOPEN, idA));
            const int iOp = findFrom(L, mark, notifNeedleForPage(NPPN_FILEOPENED, idA));
            check(iBo >= 0 && iOp >= 0 && iBo < iOp,
                  "NPPN_FILEBEFOREOPEN precedes NPPN_FILEOPENED, same buffer id");
        }

        // ---- (a) save: BEFORESAVE precedes FILESAVED, same id, exactly once ------------------------
        nibSciCall(nullptr, -1, SCI_APPENDTEXT, 6, reinterpret_cast<intptr_t>("edit1\n"));
        pump();
        mark = readLogLines().size();
        g_nibDocSave();                                        // DIRECT host call -> onSave -> writeFile
        pump();
        L = readLogLines();
        {
            const int iBs = findFrom(L, mark, notifNeedleForPage(NPPN_FILEBEFORESAVE, idA));
            const int iSv = findFrom(L, mark, notifNeedleForPage(NPPN_FILESAVED, idA));
            check(iBs >= 0 && iSv >= 0 && iBs < iSv,
                  "(a) NPPN_FILEBEFORESAVE precedes NPPN_FILESAVED with the same buffer id");
            check(countFrom(L, mark, notifNeedleForPage(NPPN_FILESAVED, idA)) == 1,
                  "(a) exactly ONE NPPN_FILESAVED per real save");
        }

        // ---- (b) undo-to-savepoint: savepoint reached, but NO false FILESAVED ----------------------
        nibSciCall(nullptr, -1, SCI_APPENDTEXT, 3, reinterpret_cast<intptr_t>("zzz"));
        pump();
        mark = readLogLines().size();
        nibSciCall(nullptr, -1, SCI_UNDO, 0, 0);               // back to the save point
        pump();
        check(coreSciCall(-1, SCI_GETMODIFY, 0, 0) == 0, "undo really returned to the save point");
        L = readLogLines();
        check(countFrom(L, mark, notifNeedle(SCN_SAVEPOINTREACHED, 0)) >= 1,
              "SCN_SAVEPOINTREACHED did fire (the undo-to-savepoint scenario really ran)");
        check(countFrom(L, mark, notifNeedleForPage(NPPN_FILESAVED, idA)) == 0,
              "(b) NO false NPPN_FILESAVED after undo-to-savepoint");
        check(countFrom(L, mark, notifNeedleForPage(NPPN_FILEBEFORESAVE, idA)) == 0,
              "(b) ...and no NPPN_FILEBEFORESAVE either (nothing was written)");

        // ---- (a, background id) Save All: per-buffer SAVING/SAVED pairs, each with its OWN id -------
        nibSciCall(nullptr, -1, SCI_APPENDTEXT, 4, reinterpret_cast<intptr_t>("yyy\n"));   // re-dirty A
        pump();
        g_nibDocOpen(std::string(fileB.utf8_str()).c_str());   // B becomes the active buffer; A goes background
        pump();
        const intptr_t idB = g_nibDocActiveId ? g_nibDocActiveId() : 0;
        check(idB != 0 && idB != idA, "opened b.txt as a second, distinct buffer");
        nibSciCall(nullptr, -1, SCI_APPENDTEXT, 4, reinterpret_cast<intptr_t>("www\n"));   // dirty B
        pump();
        mark = readLogLines().size();
        g_nibInvokeCommand(kCmdFileSaveall);                   // DIRECT dispatch through the wx command path
        pump();
        L = readLogLines();
        {
            const int aBs = findFrom(L, mark, notifNeedleForPage(NPPN_FILEBEFORESAVE, idA));
            const int aSv = findFrom(L, mark, notifNeedleForPage(NPPN_FILESAVED, idA));
            const int bBs = findFrom(L, mark, notifNeedleForPage(NPPN_FILEBEFORESAVE, idB));
            const int bSv = findFrom(L, mark, notifNeedleForPage(NPPN_FILESAVED, idB));
            check(bBs >= 0 && bSv >= 0 && bBs < bSv,
                  "(a) Save All: active buffer's BEFORESAVE->FILESAVED pair, its own id");
            check(aBs >= 0 && aSv >= 0 && aBs < aSv,
                  "(a) Save All: BACKGROUND buffer's BEFORESAVE->FILESAVED pair carries ITS id, not the active one's");
            check(countFrom(L, mark, notifNeedleForPage(NPPN_FILESAVED, idA)) == 1
                      && countFrom(L, mark, notifNeedleForPage(NPPN_FILESAVED, idB)) == 1,
                  "(a) Save All: exactly one FILESAVED per written buffer");
        }
        check(readWholeFile(fileA).Contains("yyy") && readWholeFile(fileB).Contains("www"),
              "Save All really wrote both buffers to disk (byte truth, not just events)");

        // ---- close: FILEBEFORECLOSE precedes FILECLOSED, same id -----------------------------------
        mark = readLogLines().size();
        g_nibInvokeCommand(kCmdFileClose);                     // closes the active (clean) buffer B
        pump();
        L = readLogLines();
        {
            const int iBc = findFrom(L, mark, notifNeedleForPage(NPPN_FILEBEFORECLOSE, idB));
            const int iCl = findFrom(L, mark, notifNeedleForPage(NPPN_FILECLOSED, idB));
            check(iBc >= 0 && iCl >= 0 && iBc < iCl,
                  "NPPN_FILEBEFORECLOSE precedes NPPN_FILECLOSED with the same buffer id");
        }

        // ---- close, last-document path: the recycle is still a close for plugins -------------------
        // Closing the final document recycles its page into a fresh untitled buffer (never zero
        // documents) instead of deleting it - but real N++ fires FILEBEFORECLOSE/FILECLOSED there
        // too before re-using the tab, so the host fires DOCUMENT_CLOSED on that path as well.
        g_nibInvokeCommand(kCmdFileClose);                     // drain: closes the now-active doc (normal > 1 path)
        pump();
        const intptr_t idLast = g_nibDocActiveId ? g_nibDocActiveId() : 0;
        check(idLast != 0 && g_nibDocCount && g_nibDocCount() == 1,
              "drained to a single remaining document");
        mark = readLogLines().size();
        g_nibInvokeCommand(kCmdFileClose);                     // totalDocs() == 1 -> the buffer-recycle path
        pump();
        L = readLogLines();
        {
            const int iBc = findFrom(L, mark, notifNeedleForPage(NPPN_FILEBEFORECLOSE, idLast));
            const int iCl = findFrom(L, mark, notifNeedleForPage(NPPN_FILECLOSED, idLast));
            check(iBc >= 0 && iCl >= 0 && iBc < iCl,
                  "last-document close (buffer recycle) still fires FILEBEFORECLOSE -> FILECLOSED, same id");
        }
        check(g_nibDocCount && g_nibDocCount() == 1,
              "the recycled untitled buffer remains (never zero documents)");

        // ---- Phase 3: per-view buffer model (nib.documents v5) -------------------------------------
        // Set up 3 fixture files, migrate one to the SUB view (a real split), then assert both the HOST
        // hooks directly (per-view enumeration, background-buffer encoding/EOL peeks with the active
        // view's caret preserved, background save-by-id byte-truth) and the BRIDGE router (id/pos
        // packing incl. the view bit, GETNBOPENFILES filter, UniMode/EOL round-trips) via the probe log.
        {
            const wxString p3a = work + wxFILE_SEP_PATH + "p3a.txt";
            const wxString p3b = work + wxFILE_SEP_PATH + "p3b.txt";
            const wxString p3c = work + wxFILE_SEP_PATH + "p3c.txt";
            check(writeWholeFile(p3a, "aaa aaa\r\n") && writeWholeFile(p3b, "bbb bbb\r\n") && writeWholeFile(p3c, "ccc ccc\r\n"),
                  "Phase-3 fixtures created (CRLF line endings)");
            g_nibDocOpen(std::string(p3a.utf8_str()).c_str()); pump();
            g_nibDocOpen(std::string(p3b.utf8_str()).c_str()); pump();
            g_nibDocOpen(std::string(p3c.utf8_str()).c_str()); pump();   // p3c is active (newest)
            g_nibInvokeCommand(kCmdViewGotoAnotherView); pump(120);      // migrate p3c to the SUB view
            const int mainN = g_nibDocViewCount(0), subN = g_nibDocViewCount(1);
            check(subN >= 1 && mainN >= 1, "after migration both views hold >= 1 document (real split)");

            // resolve a buffer id by its path (layout-independent)
            auto idForPath = [&](const wxString& path) -> intptr_t {
                char buf[2048];
                for (int view = 0; view < 2; ++view)
                    for (int i = 0, n = g_nibDocViewCount(view); i < n; ++i) {
                        const intptr_t id = g_nibDocIdAt(view, i);
                        const int len = g_nibDocPathFromId(id, buf, sizeof(buf));
                        if (len > 0 && wxString::FromUTF8(buf, len) == path) return id;
                    }
                return 0;
            };

            // -- host hooks: id/index round-trip in both directions, both views ----------------------
            {
                const intptr_t idM0 = g_nibDocIdAt(0, 0);
                int v = -9, i = -9;
                check(idM0 != 0 && g_nibDocPosOf(idM0, 0, &v, &i) && v == 0 && i == 0,
                      "host pos_of(id_at(0,0)) round-trips to (view 0, index 0)");
                const intptr_t idS0 = g_nibDocIdAt(1, 0);
                int vs = -9, is = -9;
                check(idS0 != 0 && g_nibDocPosOf(idS0, 0, &vs, &is) && vs == 1 && is == 0,
                      "host pos_of(id_at(1,0)) round-trips to (view 1, index 0) - the sub view");
                check(g_nibDocIdAt(0, 999) == 0 && g_nibDocPosOf(0xdead, 0, &v, &i) == 0,
                      "host id_at out-of-range -> 0 and pos_of(unknown id) -> not found");
            }

            // -- host hooks: background-buffer encoding/EOL peeks with BOTH views' carets preserved ---
            // The active view is the SUB view (p3c). Peek a MAIN-view buffer that is NOT that view's
            // mounted selection, so its document is mounted on no view - a true doc-pointer-swap peek.
            g_nibDocActivateAt(1, 0); pump();   // ensure the sub view (p3c) is the active view
            const int mainSel = g_nibDocIndexOfActive(0);
            const int bgIndex = (mainSel == 0 && mainN > 1) ? 1 : 0;   // a main page that is not main's selection
            const intptr_t bgId = g_nibDocIdAt(0, bgIndex);
            check(bgId != 0 && bgIndex != mainSel, "picked an unmounted background buffer in the main view");
            nibSciCall(nullptr, -1, SCI_GOTOPOS, 4, 0);                 // known caret on the ACTIVE (sub) view
            coreSciCall(0, SCI_GOTOPOS, 2, 0);                          // known caret on the (inactive) MAIN view
            const long long subCaret  = coreSciCall(-1, SCI_GETCURRENTPOS, 0, 0);
            const long long mainCaret = coreSciCall(0,  SCI_GETCURRENTPOS, 0, 0);
            const int eolBefore = g_nibDocEolGet(bgId);
            check(eolBefore == SC_EOL_CRLF, "host eol_get on a background buffer reads its CRLF mode (doc-swap peek)");
            check(g_nibDocEolSet(bgId, SC_EOL_LF) == 1 && g_nibDocEolGet(bgId) == SC_EOL_LF,
                  "host eol_set converts a background buffer's line endings, eol_get reads it back");
            check(g_nibDocEncodingGet(bgId) == ENC_UTF8, "host encoding_get on a background buffer -> UTF-8 (ASCII fixture)");
            check(g_nibDocEncodingSet(bgId, ENC_UTF8_BOM) == 1 && g_nibDocEncodingGet(bgId) == ENC_UTF8_BOM,
                  "host encoding_set records a background buffer's encoding (save-to-apply)");
            check(coreSciCall(-1, SCI_GETCURRENTPOS, 0, 0) == subCaret,
                  "the ACTIVE (sub) view's caret is unchanged after the background doc-pointer peeks");
            check(coreSciCall(0, SCI_GETCURRENTPOS, 0, 0) == mainCaret,
                  "the inactive MAIN view's caret is unchanged after the background doc-pointer peeks");

            // -- host hook: save a BACKGROUND buffer to disk, byte-compare -----------------------------
            const intptr_t idA = idForPath(p3a);
            check(idA != 0, "resolved p3a's buffer id by path");
            { int va, ia; check(g_nibDocPosOf(idA, 0, &va, &ia) && g_nibDocActivateAt(va, ia) == 1, "activated p3a"); }
            pump();
            nibSciCall(nullptr, -1, SCI_APPENDTEXT, 8, reinterpret_cast<intptr_t>("SAVEDBG\n"));   // edit p3a while active
            pump();
            g_nibDocActivateAt(1, 0); pump();   // back to the sub view -> p3a is a background buffer now
            check(g_nibDocSaveById(idA) == 1, "host save_by_id wrote the background buffer p3a");
            check(readWholeFile(p3a).Contains("SAVEDBG"),
                  "save_by_id really wrote p3a's edit to disk (byte truth, background buffer)");

            // -- host hook: rename_untitled rejects a pathful name, accepts a bare label ---------------
            {
                char pbuf[16];
                intptr_t idUntitled = 0;   // the empty-path (untitled) buffer - path_from_id returns 0 for it
                for (int view = 0; view < 2 && !idUntitled; ++view)
                    for (int i = 0, n = g_nibDocViewCount(view); i < n; ++i) {
                        const intptr_t id = g_nibDocIdAt(view, i);
                        if (g_nibDocPathFromId(id, pbuf, sizeof(pbuf)) == 0) { idUntitled = id; break; }
                    }
                check(idUntitled != 0, "found the untitled buffer (no on-disk path)");
                check(g_nibDocRenameUntitled(idUntitled, "has/slash") == 0, "rename_untitled rejects a path-like name");
                check(g_nibDocRenameUntitled(idUntitled, "Scratch") == 1, "rename_untitled accepts a bare tab label");
                check(g_nibDocRenameUntitled(idA, "nope") == 0, "rename_untitled rejects a buffer that has an on-disk path");
            }

            // -- host hook: tab_color_id (no colour -> -1) -------------------------------------------
            check(g_nibDocTabColorId(1, 0) == -1, "tab_color_id on an uncoloured tab -> -1");

            // -- BRIDGE router: fire the probe's Phase-3 table (needs the sub view active) -------------
            g_nibDocActivateAt(1, 0); pump();
            const size_t p3mark = readLogLines().size();
            g_nibInvokeCommand(cFirst + 1);     // the 2nd allocated cmd id -> probe messageProc -> runPhase3Table
            pump(150);
            std::vector<std::string> P = readLogLines();
            P.erase(P.begin(), P.begin() + std::min(p3mark, P.size()));

            check(p3val(P, "nb.primary") == g_nibDocViewCount(0) && p3val(P, "nb.second") == g_nibDocViewCount(1),
                  "GETNBOPENFILES(PRIMARY/SECOND) match the per-view counts");
            check(p3val(P, "nb.all") == p3val(P, "nb.primary") + p3val(P, "nb.second"),
                  "GETNBOPENFILES(ALL) == primary + second");
            check(p3val(P, "idx.sub") >= 0, "GETCURRENTDOCINDEX(SUB_VIEW) reports the sub view's active index");
            check(p3val(P, "pos.m0") == 0,
                  "GETPOSFROMBUFFERID(main index 0) packs (view 0, index 0) == 0");
            check(p3val(P, "pos.s0") == (static_cast<long long>(1) << 30),
                  "GETPOSFROMBUFFERID(sub index 0) sets the view bit: (1<<30)");
            check(p3val(P, "cur") != LLONG_MIN && p3val(P, "cur.back") == p3val(P, "cur"),
                  "GETBUFFERIDFROMPOS(unpack(GETPOSFROMBUFFERID(cur))) round-trips back to the same id");
            check(p3val(P, "enc.after") == 1,
                  "SETBUFFERENCODING(uniUTF8) then GETBUFFERENCODING == 1 (UniMode round-trip)");
            check(p3val(P, "eol.after") == SC_EOL_LF,
                  "SETBUFFERFORMAT(LF) then GETBUFFERFORMAT == LF (EOL round-trip)");
            check(p3val(P, "tabcolor") == -1, "GETTABCOLORID on an uncoloured current tab -> -1");
            check(p3val(P, "makedirty") == TRUE, "MAKECURRENTBUFFERDIRTY answered TRUE");
            check(coreSciCall(-1, SCI_GETMODIFY, 0, 0) != 0, "...and the active buffer is really modified now");

            // clean up: close every Phase-3 buffer back to a single untitled document so the tail
            // assertions (the TBMODIFICATION-once tripwire) run against a settled model. Force-close via
            // the file-close command; dirty buffers are discarded (m_askBeforeClose is off in the sandbox).
            for (int guard = 0; g_nibDocCount && g_nibDocCount() > 1 && guard < 20; ++guard) {
                g_nibInvokeCommand(kCmdFileClose);
                pump();
            }
            check(g_nibDocCount && g_nibDocCount() == 1, "Phase-3 buffers closed - back to a single document");
        }

        // ---- Phase 4: event fidelity (opt-in mask, real modified flags, LANGCHANGED, shortcut read) --
        // Arm the probe's opt-in (3rd allocated cmd id -> runPhase4Table -> NPPM_ADDSCNMODIFIEDFLAGS for
        // SC_PERFORMED_UNDO|SC_MOD_BEFOREDELETE), then drive insert (excluded flags -> silent) and undo
        // (included flags -> NPPN_GLOBALMODIFIED carrying them), a language switch (single NPPN_LANGCHANGED),
        // and check the effective-shortcut read hook both ways. The BEFORESHUTDOWN/CANCELSHUTDOWN/SHUTDOWN
        // ordering is exercised at the very end (the real close), with the SHUTDOWN-once check in main().
        {
            g_nibInvokeCommand(cFirst + 2);   // fire the probe's Phase-4 table (opt-in + a GETSHORTCUTBYCMDID probe)
            pump(120);
            check(g_nibModifiedMask == static_cast<unsigned>(SC_PERFORMED_UNDO | SC_MOD_BEFOREDELETE),
                  "NPPM_ADDSCNMODIFIEDFLAGS pushed the opted-in union mask into the host (perf gate armed)");
            {
                std::vector<std::string> P = readLogLines();
                check(p4val(P, "add") == TRUE, "NPPM_ADDSCNMODIFIEDFLAGS answered TRUE");
                check(p4val(P, "sc.unbound") == FALSE,
                      "NPPM_GETSHORTCUTBYCMDID(this plugin's unbound cmd) -> FALSE");
                check(p4val(P, "sc.unbound.key") == 0xEE,
                      "...and a FALSE return left the caller's ShortcutKey._key untouched (0xEE sentinel)");
                // NPPM_GETSHORTCUTBYCMDID on a PUNCTUATION-bound host command (View > Zoom In, "Ctrl+="):
                // catches wxKeyToVk regressing to raw ASCII ('=' == 0x3D) instead of the real Win32
                // VK_OEM_PLUS (0xBB) - the letter-only Ctrl+S case above can't exercise this path.
                check(p4val(P, "sc.zoomin") == TRUE,
                      "NPPM_GETSHORTCUTBYCMDID(View>Zoom In, Ctrl+=) -> TRUE (bound)");
                check(p4val(P, "sc.zoomin.ctrl") == 1,
                      "...ShortcutKey._isCtrl set");
                check(p4val(P, "sc.zoomin.key") == 0xBB,
                      "...ShortcutKey._key is the real VK_OEM_PLUS (0xBB), not raw ASCII '=' (0x3D)");
            }

            // a dedicated fixture so the undo history is exactly our single edit
            const wxString p4file = work + wxFILE_SEP_PATH + "p4.txt";
            check(writeWholeFile(p4file, "modme\n"), "Phase-4 fixture created");
            g_nibDocOpen(std::string(p4file.utf8_str()).c_str());
            pump();
            const intptr_t idP4 = g_nibDocActiveId ? g_nibDocActiveId() : 0;
            check(idP4 != 0, "opened p4.txt as the active buffer");

            // (1) insert -> SC_MOD_INSERTTEXT is NOT in the opted-in mask -> no NPPN_GLOBALMODIFIED
            size_t m4 = readLogLines().size();
            nibSciCall(nullptr, -1, SCI_APPENDTEXT, 7, reinterpret_cast<intptr_t>("insert\n"));
            pump();
            check(countGm(readLogLines(), m4) == 0,
                  "no NPPN_GLOBALMODIFIED for an insert (opted-in flags exclude SC_MOD_INSERTTEXT) - mask gate works");

            // (2) undo -> SC_PERFORMED_UNDO (+ SC_MOD_BEFOREDELETE) ARE in the mask -> GLOBALMODIFIED carries them
            size_t m4b = readLogLines().size();
            nibSciCall(nullptr, -1, SCI_UNDO, 0, 0);
            pump();
            L = readLogLines();
            check(countGm(L, m4b) >= 1,
                  "undo fired NPPN_GLOBALMODIFIED (opted-in SC_PERFORMED_UNDO matched the mask)");
            const unsigned gmOr = gmOrFrom(L, m4b);
            check((gmOr & SC_PERFORMED_UNDO) && (gmOr & SC_MOD_BEFOREDELETE),
                  "NPPN_GLOBALMODIFIED carries the REAL opted-in modificationType flags (UNDO + BEFOREDELETE)");

            // (3) NPPN_LANGCHANGED: exactly once, carrying the changed buffer's id
            size_t m4c = readLogLines().size();
            check(g_nibDocSetLangById && g_nibDocSetLangById(idP4, kCmdLangC) == 1,
                  "forced language C on the active buffer (set_lang_by_id)");
            pump();
            check(countFrom(readLogLines(), m4c, notifNeedleForPage(NPPN_LANGCHANGED, idP4)) == 1,
                  "NPPN_LANGCHANGED fired exactly once with the changed buffer's id");

            // direct host hook: nib.keymap effective_shortcut, positive (Ctrl+S) and negative (unbound) paths
            {
                uint32_t mods = 0xEE, key = 0xEE;
                check(g_nibKmEffectiveShortcut && g_nibKmEffectiveShortcut(kCmdFileSave, &mods, &key) == 1
                          && (mods & 1) && !(mods & 2) && !(mods & 4) && key == static_cast<uint32_t>('S'),
                      "nib.keymap effective_shortcut(File>Save) -> Ctrl+S (Ctrl bit + key 'S')");
                mods = 0xEE; key = 0xEE;
                check(g_nibKmEffectiveShortcut(cFirst, &mods, &key) == 0 && mods == 0xEE && key == 0xEE,
                      "nib.keymap effective_shortcut(an unbound id) -> 0, out-params untouched");
            }

            // clean up back to a single document (the shutdown test dirties whatever remains)
            for (int guard = 0; g_nibDocCount && g_nibDocCount() > 1 && guard < 12; ++guard) {
                g_nibInvokeCommand(kCmdFileClose);
                pump();
            }
            check(g_nibDocCount && g_nibDocCount() == 1, "Phase-4 fixtures closed - back to a single document");
        }

        // ---- Phase 5: sessions + UI-chrome state + lexer registry ----------------------------------
        // (A) DIRECT host-hook session save->parse->load round-trip (deterministic), then (B) the BRIDGE
        // router via the probe's Phase-5 table (visibility toggle+read-back, line-number width mode,
        // CREATELEXER, GETNBUSERLANG, and a full session save/enumerate/load through the NPPM_* path).
        {
            // (A) save the currently-open saved files as a session, parse the N++ shape, load it back ----
            g_nibDocOpen(std::string(fileA.utf8_str()).c_str()); pump();   // one saved file in the session
            const wxString sessPath = g_sandboxRoot + wxFILE_SEP_PATH + "hostsave.session";
            const std::string sessPathU = std::string(sessPath.utf8_str());
            check(g_nibSessSaveCurrent && g_nibSessSaveCurrent(sessPathU.c_str()) == 1,
                  "nib.session save_current wrote a session of the open files");
            {
                wxXmlDocument doc;
                bool okLoad; { wxLogNull nl; okLoad = doc.Load(sessPath) && doc.GetRoot() != nullptr; }
                check(okLoad, "the written session file is well-formed XML");
                bool shapeOk = false, sawFileA = false;
                if (okLoad) {
                    check(doc.GetRoot()->GetName() == "NotepadPlus",
                          "session root is <NotepadPlus> (Notepad++-session-parseable, not just round-trippable)");
                    for (wxXmlNode* s = doc.GetRoot()->GetChildren(); s; s = s->GetNext()) {
                        if (s->GetName() != "Session") continue;
                        for (wxXmlNode* v = s->GetChildren(); v; v = v->GetNext()) {
                            if (v->GetName() != "mainView" && v->GetName() != "subView") continue;
                            for (wxXmlNode* f = v->GetChildren(); f; f = f->GetNext()) {
                                if (f->GetName() != "File") continue;
                                shapeOk = true;
                                if (f->GetAttribute("filename") == fileA) sawFileA = true;
                            }
                        }
                    }
                }
                check(shapeOk, "session has <Session>/<mainView|subView>/<File> nodes (the N++ schema shape)");
                check(sawFileA, "a saved <File> carries the open file's path in its filename attribute");
            }
            int valid = 0;
            check(g_nibSessFileCount && g_nibSessFileCount(sessPathU.c_str(), &valid) == 1 && valid == 1,
                  "nib.session file_count == 1 and reports a valid session XML");
            {
                char fbuf[2048] = {0};
                const int fl = g_nibSessFileAt ? g_nibSessFileAt(sessPathU.c_str(), 0, fbuf, (int)sizeof(fbuf)) : 0;
                check(fl > 0 && wxString::FromUTF8(fbuf, fl) == fileA,
                      "nib.session file_at(0) == the saved file path");
            }
            auto fileOpen = [&](const wxString& path) -> bool {
                char buf[2048];
                for (int view = 0; view < 2; ++view)
                    for (int i = 0, nn = g_nibDocViewCount(view); i < nn; ++i) {
                        const int len = g_nibDocPathFromId(g_nibDocIdAt(view, i), buf, (int)sizeof(buf));
                        if (len > 0 && wxString::FromUTF8(buf, len) == path) return true;
                    }
                return false;
            };
            // close everything (fileA included), then LOAD the session back -> fileA must re-open
            for (int guard = 0; g_nibDocCount && g_nibDocCount() > 1 && guard < 12; ++guard) { g_nibInvokeCommand(kCmdFileClose); pump(); }
            check(!fileOpen(fileA), "drained the session's file before the load round-trip");
            check(g_nibSessLoad && g_nibSessLoad(sessPathU.c_str()) == 1,
                  "nib.session load parsed and opened the session");
            pump();
            check(fileOpen(fileA), "the session's file re-opened after load (save -> parse -> load round-trip)");

            // (B) the BRIDGE router: open a 2nd saved fixture so the bridge session save is non-empty ----
            g_nibDocOpen(std::string(fileB.utf8_str()).c_str()); pump();
            const size_t p5mark = readLogLines().size();
            g_nibInvokeCommand(cFirst + 3);   // the 4th allocated cmd id -> probe messageProc -> runPhase5Table
            pump(150);
            std::vector<std::string> P = readLogLines();
            P.erase(P.begin(), P.begin() + std::min(p5mark, P.size()));

            // visibility: each flag toggled and read back through its paired IS* message (via the router)
            check(p5val(P, "tb.hidden") == TRUE && p5val(P, "tb.shown") == FALSE,
                  "toolbar: HIDETOOLBAR(TRUE)->ISTOOLBARHIDDEN==TRUE, HIDETOOLBAR(FALSE)->FALSE");
            check(p5val(P, "sb.hidden") == TRUE && p5val(P, "sb.shown") == FALSE,
                  "statusbar: HIDESTATUSBAR/ISSTATUSBARHIDDEN toggles and reads back");
            check(p5val(P, "dl.shown") == TRUE && p5val(P, "dl.hidden") == FALSE,
                  "doc-list: SHOWDOCLIST/ISDOCLISTSHOWN toggles and reads back");
            check(p5val(P, "menu.hidden") == FALSE,
                  "menubar: HIDEMENU is a portable documented no-op (ISMENUHIDDEN stays FALSE) - no per-platform detach hack");
            check(p5val(P, "lnw.constant") == LINENUMWIDTH_CONSTANT && p5val(P, "lnw.dynamic") == LINENUMWIDTH_DYNAMIC,
                  "SETLINENUMBERWIDTHMODE / GETLINENUMBERWIDTHMODE round-trips (constant<->dynamic)");
            check(p5val(P, "autoindent") != LLONG_MIN, "ISAUTOINDENTON answered");
            check(p5val(P, "iconset") == (g_nibUiIconSet ? g_nibUiIconSet() : -1),
                  "GETTOOLBARICONSETCHOICE == the host's real icon-set choice");
            check(p5val(P, "lexer.cpp") == 1,
                  "CREATELEXER(\"cpp\") returns a non-null ILexer (Lexilla, cross-platform)");
            check(p5val(P, "lexer.applied") == 1,
                  "the created ILexer applies via SCI_SETILEXER without crashing");
            check(p5val(P, "lexer.empty") == 0, "CREATELEXER(\"\") returns NULL (empty/unknown lexer name)");
            check(p5val(P, "nbuserlang") == (g_nibLexerUserLangCount ? g_nibLexerUserLangCount() : -1),
                  "GETNBUSERLANG == the host's registered Scintillua-language count");
            check(p5val(P, "sess.save") == 1 && p5val(P, "sess.valid") == 1 && p5val(P, "sess.nb") >= 1,
                  "bridge SAVECURRENTSESSION wrote a valid session; GETNBSESSIONFILES counts + validates it");
            check(p5val(P, "sess.get") == TRUE && p5val(P, "sess.f0len") > 0,
                  "bridge GETSESSIONFILES filled the caller's array with non-empty paths");
            check(p5val(P, "sess.load") == 1, "bridge LOADSESSION opened the session's files");
            {
                wxXmlDocument doc; bool okLoad; { wxLogNull nl; okLoad = doc.Load(probeSessionPathStr()) && doc.GetRoot() != nullptr; }
                check(okLoad && doc.GetRoot()->GetName() == "NotepadPlus",
                      "the bridge-written session file parses as a <NotepadPlus> session (N++-parseable)");
            }
            // the toggles restored the frame; the host state getters agree with the frame members
            check(g_nibUiChromeGet && g_nibUiChromeGet(0) == 1 && g_nibUiChromeGet(2) == 1,
                  "toolbar + statusbar restored to shown after the toggle test");

            // clean up back to a single document for the tail assertions
            for (int guard = 0; g_nibDocCount && g_nibDocCount() > 1 && guard < 20; ++guard) { g_nibInvokeCommand(kCmdFileClose); pump(); }
            check(g_nibDocCount && g_nibDocCount() == 1, "Phase-5 buffers closed - back to a single document");
        }

        // ---- Phase 6: long-tail file lifecycle + the -pluginMessage delivery -----------------------
        // Every Phase-6 notification is driven by a DIRECT host call (no modal dialog, no OS input
        // injection): the programmatic rename/delete seams (g_nibRenameActive/g_nibRecycleActive - the
        // dialog-free cores of renameFile()/recycleFile()), the Sort command (DOCORDERCHANGED), the
        // read-only toggle command (READONLYCHANGED), a fresh open (FILEBEFORELOAD), and
        // nibFireCmdlinePluginMsg (the -pluginMessage -> NPPN_CMDLINEPLUGINMSG path). Each asserts the
        // probe's beNotified log carries the right NPPN_* code + buffer id, and the before/after ordering.
        {
            const wxString p6a = work + wxFILE_SEP_PATH + "p6a.txt";
            const wxString p6b = work + wxFILE_SEP_PATH + "p6b.txt";
            check(writeWholeFile(p6a, "rename me\n") && writeWholeFile(p6b, "second\n"),
                  "Phase-6 fixtures created");

            // -- SNAPSHOTDIRTYFILELOADED: main() seeded a Recovery/seed1 manifest entry + .bak file into
            // the sandbox BEFORE wxEntry(), so WxnApp::OnInit()'s restoreSession() -> restoreRecoveryBackups()
            // already restored it (and fired the event) before runAll() ever started - search from index 0,
            // id-agnostic (the fresh recovered page's id was never captured by this harness).
            check(findFrom(readLogLines(), 0, notifNeedlePrefix(NPPN_SNAPSHOTDIRTYFILELOADED)) >= 0,
                  "NPPN_SNAPSHOTDIRTYFILELOADED fired for the seeded dirty-recovery backup restored at startup");

            // -- FILEBEFORELOAD (id 0) precedes FILEOPENED (the opened file's id) --
            mark = readLogLines().size();
            g_nibDocOpen(std::string(p6a.utf8_str()).c_str());
            pump();
            const intptr_t id6a = g_nibDocActiveId ? g_nibDocActiveId() : 0;
            check(id6a != 0, "opened p6a.txt (active buffer id is non-zero)");
            L = readLogLines();
            {
                const int iBl = findFrom(L, mark, notifNeedle(NPPN_FILEBEFORELOAD, 0));
                const int iOp = findFrom(L, mark, notifNeedleForPage(NPPN_FILEOPENED, id6a));
                check(iBl >= 0 && iOp >= 0 && iBl < iOp,
                      "NPPN_FILEBEFORELOAD (id 0) precedes NPPN_FILEOPENED for the opened file");
            }

            // -- READONLYCHANGED carries the buffer id on a read-only toggle --
            mark = readLogLines().size();
            g_nibInvokeCommand(kCmdEditToggleReadOnly);   // -> toggleReadOnly()
            pump();
            check(findFrom(readLogLines(), mark, notifNeedleForPage(NPPN_READONLYCHANGED, id6a)) >= 0,
                  "NPPN_READONLYCHANGED fired with the buffer id when read-only was toggled");
            g_nibInvokeCommand(kCmdEditToggleReadOnly); pump();   // toggle back to writable

            // -- rename triple: BEFORE_RENAME precedes RENAMED, same id (the page id survives the rename) --
            const wxString p6aRenamed = work + wxFILE_SEP_PATH + "p6a-renamed.txt";
            mark = readLogLines().size();
            check(g_nibRenameActive && g_nibRenameActive(std::string(p6aRenamed.utf8_str()).c_str()) == 1,
                  "programmatic rename of the active file succeeded");
            pump();
            L = readLogLines();
            {
                const int iBr = findFrom(L, mark, notifNeedleForPage(NPPN_FILEBEFORERENAME, id6a));
                const int iRn = findFrom(L, mark, notifNeedleForPage(NPPN_FILERENAMED, id6a));
                check(iBr >= 0 && iRn >= 0 && iBr < iRn,
                      "NPPN_FILEBEFORERENAME precedes NPPN_FILERENAMED with the same buffer id");
            }
            check(wxFileExists(p6aRenamed) && !wxFileExists(p6a), "rename really moved the file on disk");

            // -- rename cancel: a rename into a nonexistent directory fails -> BEFORE_RENAME then RENAMECANCEL --
            // wxLogNull: the underlying wxRenameFile() logs this expected failure via wxLogSysError, which
            // a GUI app's default log target renders as a modal error dialog - exactly the kind of real,
            // un-auto-answered popup this headless harness must never leave sitting on screen (the
            // CloseDialogHook only answers the "wxNote" confirm-close prompt, not a generic wxLog dialog).
            const wxString badPath = work + wxFILE_SEP_PATH + "no_such_dir" + wxFILE_SEP_PATH + "x.txt";
            mark = readLogLines().size();
            { wxLogNull noLog; check(g_nibRenameActive(std::string(badPath.utf8_str()).c_str()) == 0,
                  "rename into a nonexistent directory fails"); }
            pump();
            L = readLogLines();
            {
                const int iBr = findFrom(L, mark, notifNeedleForPage(NPPN_FILEBEFORERENAME, id6a));
                const int iRc = findFrom(L, mark, notifNeedleForPage(NPPN_FILERENAMECANCEL, id6a));
                check(iBr >= 0 && iRc >= 0 && iBr < iRc,
                      "a failed rename fires NPPN_FILEBEFORERENAME then NPPN_FILERENAMECANCEL, same id");
            }

            // -- DOCORDERCHANGED: open a 2nd file, Sort by name -> the tab order changes --
            g_nibDocOpen(std::string(p6b.utf8_str()).c_str()); pump();
            check(g_nibDocCount && g_nibDocCount() >= 2, "two documents open for the sort");
            const intptr_t idSortActive = g_nibDocActiveId ? g_nibDocActiveId() : 0;   // sortTabs carries the active page's id
            mark = readLogLines().size();
            g_nibInvokeCommand(kCmdWindowSortFnAsc); pump();   // -> sortTabs(Name, asc)
            check(findFrom(readLogLines(), mark, notifNeedleForPage(NPPN_DOCORDERCHANGED, idSortActive)) >= 0,
                  "NPPN_DOCORDERCHANGED fired when the tab order changed (Sort by name)");

            // -- delete triple: re-activate the renamed file, delete it -> BEFORE_DELETE precedes DELETED --
            { int dv = -1, di = -1;
              check(g_nibDocPosOf(id6a, 0, &dv, &di) && g_nibDocActivateAt(dv, di) == 1,
                    "re-activated the renamed file for the delete test"); }
            pump();
            check(g_nibDocActiveId && g_nibDocActiveId() == id6a, "the renamed file is the active buffer before delete");
            mark = readLogLines().size();
            check(g_nibRecycleActive && g_nibRecycleActive() == 1, "programmatic delete of the active file succeeded");
            pump();
            L = readLogLines();
            {
                const int iBd = findFrom(L, mark, notifNeedleForPage(NPPN_FILEBEFOREDELETE, id6a));
                const int iDl = findFrom(L, mark, notifNeedleForPage(NPPN_FILEDELETED, id6a));
                check(iBd >= 0 && iDl >= 0 && iBd < iDl,
                      "NPPN_FILEBEFOREDELETE precedes NPPN_FILEDELETED with the same buffer id");
            }
            check(!wxFileExists(p6aRenamed), "delete really removed the file from disk");

            // -- delete failure: remove the file out from under its OPEN buffer, then attempt the same
            // programmatic delete again -> BEFORE_DELETE still fires (the buffer still has a path to try),
            // but the actual removal fails (already gone) -> DELETE_FAILED, not DELETED. Cross-platform:
            // POSIX remove() fails ENOENT; Windows SHFileOperationW likewise reports the file not found.
            const wxString p6c = work + wxFILE_SEP_PATH + "p6c.txt";
            check(writeWholeFile(p6c, "will vanish\n"), "p6c.txt fixture created");
            g_nibDocOpen(std::string(p6c.utf8_str()).c_str()); pump();
            const intptr_t id6c = g_nibDocActiveId ? g_nibDocActiveId() : 0;
            check(id6c != 0, "opened p6c.txt (active buffer id is non-zero)");
            check(wxRemoveFile(p6c), "removed p6c.txt out from under its open buffer (simulates external deletion)");
            mark = readLogLines().size();
            // wxLogNull: on POSIX, recycleActive()'s fallback path calls wxRemoveFile() again on the
            // already-gone file, whose expected ENOENT failure wxRemoveFile() reports via wxLogSysError -
            // same un-auto-answered-dialog risk as the rename-cancel case above (Windows takes the
            // SHFileOperationW branch instead, which never logs, but the guard is harmless there too).
            { wxLogNull noLog; check(g_nibRecycleActive && g_nibRecycleActive() == 0,
                  "programmatic delete of an already-gone file reports failure"); }
            pump();
            L = readLogLines();
            {
                const int iBd = findFrom(L, mark, notifNeedleForPage(NPPN_FILEBEFOREDELETE, id6c));
                const int iDf = findFrom(L, mark, notifNeedleForPage(NPPN_FILEDELETEFAILED, id6c));
                check(iBd >= 0 && iDf >= 0 && iBd < iDf,
                      "NPPN_FILEBEFOREDELETE precedes NPPN_FILEDELETEFAILED, same id, when the delete fails");
            }
            check(g_nibDocActiveId && g_nibDocActiveId() == id6c,
                  "a failed delete leaves the buffer open (no close on failure)");

            // -- -pluginMessage: nibFireCmdlinePluginMsg delivers NPPN_CMDLINEPLUGINMSG (id 0) to the plugin --
            mark = readLogLines().size();
            nibFireCmdlinePluginMsg("compare:left=a.txt&right=b.txt");   // the host's CLI-delivery seam
            pump();
            check(findFrom(readLogLines(), mark, notifNeedle(NPPN_CMDLINEPLUGINMSG, 0)) >= 0,
                  "nibFireCmdlinePluginMsg delivered NPPN_CMDLINEPLUGINMSG to the plugin (-pluginMessage path)");

            // -- -pluginMessage over the "reuse an existing window" IPC handoff: the OTHER delivery path,
            // which used to drop the text silently (the payload never carried it, and OnExec never parsed
            // it). Drive the REAL wire format through WxnIpcConnection::OnExec - the same virtual call a
            // second wxnote process's Execute() lands on - end-to-end into g_ipcOpenRequest's
            // nibFireCmdlinePluginMsg call, not just the direct CLI seam exercised above.
            //
            // Heap-allocated and DELIBERATELY never deleted: wxDDEConnection's destructor (the Windows
            // backend wxConnection aliases to) unconditionally dereferences m_client when m_server is
            // null (build/_deps/wxwidgets-src/src/msw/dde.cpp ~line 511-512) - safe only for a connection
            // wx's own DDE handshake produced via OnAcceptConnection/MakeConnection (which sets one of
            // those), never for one constructed bare like this. OnExec() itself touches none of that
            // base-class connection state, so calling it directly is safe; only destructing it isn't.
            // A single leaked object in a one-shot test binary is harmless and keeps this portable (no
            // reach into wx's Windows-only DDE internals to work around it, no #ifdef _WIN32 special case).
            mark = readLogLines().size();
            {
                auto* conn = new WxnIpcConnection();
                check(conn->OnExec(wxEmptyString, "\x01PLUGINMSG=ipc-handoff-message\n"),
                      "WxnIpcConnection::OnExec accepted a PLUGINMSG= reuse-window payload");
            }
            pump();
            check(findFrom(readLogLines(), mark, notifNeedle(NPPN_CMDLINEPLUGINMSG, 0)) >= 0,
                  "-pluginMessage survives the reuse-window IPC handoff (OnExec -> g_ipcOpenRequest -> NPPN_CMDLINEPLUGINMSG)");

            // clean up back to a single document for the tail assertions
            for (int guard = 0; g_nibDocCount && g_nibDocCount() > 1 && guard < 20; ++guard) { g_nibInvokeCommand(kCmdFileClose); pump(); }
            check(g_nibDocCount && g_nibDocCount() == 1, "Phase-6 buffers closed - back to a single document");
        }

        // ---- (d) allocated command ids round-trip through the wx dispatcher ------------------------
        check(cFirst > 32767,
              "(d) allocated ids sit above 32767 (WM_COMMAND sign-wraps them; wrapped dispatch driven below)");
        for (int k = 0; k < 2; ++k)
        {
            // k=0 dispatches the TRUE id (what a wx-native menu/toolbar event carries). k=1 dispatches
            // the SIGN-WRAPPED value MSW's 16-bit WM_COMMAND path would sign-extend it to (64xxx wraps
            // negative): onCommand's & 0xFFFF mask must recover the real id, so the probe must still
            // log the TRUE id. A positive id alone cannot drive that recovery branch (x & 0xFFFF is
            // the identity below 65536), so this is what actually exercises the sign-wrap hazard.
            const int trueId = cFirst + k;
            const int sentId = (k == 0) ? trueId : trueId - 65536;
            char what[200], needle[64];
            mark = readLogLines().size();
            g_nibInvokeCommand(sentId);                        // wx-event dispatch -> alloc sinks -> bridge -> probe
            pump();
            L = readLogLines();
            std::snprintf(needle, sizeof(needle), "{\"k\":\"cmd\",\"id\":%d}", trueId);
            std::snprintf(what, sizeof(what),
                          "(d) allocated cmd id %d (dispatched as %d) reaches the probe's messageProc via the wx dispatcher",
                          trueId, sentId);
            check(findFrom(L, mark, needle) >= 0, what);
        }

        // ---- NPPN_TBMODIFICATION fired exactly once across the whole session -----------------------
        // Today once-per-boot holds by construction (both plugin loaders run inside the bridge's
        // activate() BEFORE its single notifyNpp(NPPN_TBMODIFICATION)); this assertion pins it as
        // CONTRACT. If a runtime/late plugin-load feature ever lands, it must emit TBMODIFICATION
        // once per late-loaded plugin (so that plugin's toolbar-init path runs) - this tripwire is
        // what forces that design decision instead of letting late loaders silently never hear it.
        L = readLogLines();
        check(countFrom(L, 0, notifNeedle(NPPN_TBMODIFICATION, 0)) == 1,
              "NPPN_TBMODIFICATION delivered exactly once for this plugin-load pass");

        // ---- Phase 4: BEFORESHUTDOWN / CANCELSHUTDOWN on a vetoed close, then the real close --------
        // Dirty the remaining buffer so confirmClose reaches the (headlessly auto-answered) save prompt.
        nibSciCall(nullptr, -1, SCI_APPENDTEXT, 10, reinterpret_cast<intptr_t>("dirtyexit\n"));
        pump();
        check(coreSciCall(-1, SCI_GETMODIFY, 0, 0) != 0, "active buffer dirtied for the shutdown-veto test");

        // (1) a VETOED close: the hook answers Cancel -> confirmClose returns false. Expect the
        //     BEFORESHUTDOWN -> CANCELSHUTDOWN pair and NO SHUTDOWN; the frame survives.
        g_closeAnswer = wxID_CANCEL;
        mark = readLogLines().size();
        if (wxWindow* top = GetTopWindow()) top->Close(false);   // CanVeto() -> the veto path runs
        pump(80);
        L = readLogLines();
        {
            const int iBefore = findFrom(L, mark, notifNeedle(NPPN_BEFORESHUTDOWN, 0));
            const int iCancel = findFrom(L, mark, notifNeedle(NPPN_CANCELSHUTDOWN, 0));
            check(iBefore >= 0, "NPPN_BEFORESHUTDOWN fired on the close attempt");
            check(iCancel >= 0 && iCancel > iBefore,
                  "NPPN_CANCELSHUTDOWN fired after BEFORESHUTDOWN (the cancelled shutdown)");
            check(countFrom(L, mark, notifNeedle(NPPN_SHUTDOWN, 0)) == 0,
                  "NO NPPN_SHUTDOWN on a vetoed close (the app stays up)");
        }
        check(GetTopWindow() != nullptr, "the frame survived the vetoed close");

        std::printf("  ..  runAll checks done; driving the real shutdown (SHUTDOWN-once verified in main)\n");
        std::fflush(stdout);

        // (2) the REAL close: forced (CanVeto()==false) skips the prompt loop entirely -> teardown ->
        //     CallAfter-deferred unloadNibPlugins -> deactivate -> the single NPPN_SHUTDOWN. That fires
        //     AFTER this function returns, so main() (post-wxEntry) makes the "exactly once" assertion.
        g_closeAnswer = wxID_NO;   // disarm the veto (a forced close does not consult it anyway)
        if (wxWindow* top = GetTopWindow()) top->Close(true);
    }
};

wxIMPLEMENT_APP_NO_MAIN(BridgeSelfTestApp);

int main(int argc, char** argv)
{
    // The host's plugin-facing command ids DELIBERATELY sit above 32767 (NIB_CMD_BASE 63000+, the
    // nib.alloc pool 64000+ - recovered from the 16-bit WM_COMMAND path by onCommand's & 0xFFFF
    // mask), which trips wx's advisory wxMenuItemBase "invalid itemid value" assert on every
    // Extensions-menu append in an assert-enabled wx build (wxDEBUG_LEVEL defaults to 1 even in
    // release). The shipped GUI app swallows exactly that one assertion via WxnApp::OnAssertFailure
    // (see main.cpp) so it never reaches a user's screen. Here we reach for the blunter
    // wxDisableAsserts() instead: this is a non-interactive console harness, so ANY wx assertion -
    // not just the itemid one - must be prevented from popping wx's modal debug MessageBox (which,
    // with no one to click it, would hang CI) or abort()ing the process mid-run.
    wxDisableAsserts();

    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path root = fs::temp_directory_path(ec);
    if (ec) root = fs::path(".");
    root /= "wxnote_bridge_selftest";
    fs::remove_all(root, ec);                       // hermetic: every run starts from a blank sandbox
    fs::create_directories(root / "userdata", ec);
    g_sandboxRoot     = wxString::FromUTF8(root.u8string().c_str());
    g_sandboxUserData = wxString::FromUTF8((root / "userdata").u8string().c_str());

#ifdef _WIN32
    // Drop a stale probe log (the pid-stamp check would catch it anyway; this keeps runs tidy).
    wchar_t exe[MAX_PATH] = L"";
    ::GetModuleFileNameW(nullptr, exe, MAX_PATH);
    fs::remove(fs::path(exe).parent_path() / "plugins" / "Config" / "wxn_probe.jsonl", ec);
#else
    // POSIX: the bridge scans <user-data>/plugins/<Name>/<Name>.so - stage the probe there from the
    // build tree (bin/nib/example/example_plugin.so, the recompiled-plugin CI artifact).
    fs::path exeDir = fs::absolute(fs::path(argv[0]), ec).parent_path();
    const char* soName =
#ifdef __APPLE__
        "example_plugin.dylib";
#else
        "example_plugin.so";
#endif
    fs::path probeSrc = exeDir / "nib" / "example" / soName;
    fs::path probeDir = root / "userdata" / "plugins" / "example_plugin";
    fs::create_directories(probeDir, ec);
    fs::copy_file(probeSrc, probeDir / soName, fs::copy_options::overwrite_existing, ec);
#endif

    const int rc = wxEntry(argc, argv);

    // NPPN_SHUTDOWN is delivered from the CallAfter-deferred unloadNibPlugins() - i.e. AFTER runAll()
    // returned and the app finished exiting - so it can only be asserted here, once wxEntry has come back.
    {
        const std::vector<std::string> L = readLogLines();
        const std::string needle = notifNeedle(NPPN_SHUTDOWN, 0);
        int n = 0;
        for (const auto& s : L) if (s.find(needle) != std::string::npos) ++n;
        check(n == 1, "NPPN_SHUTDOWN delivered exactly once, at real exit (CallAfter-deferred unload path)");
    }
    // The nib.documents v5 hooks (main.cpp:2608-2721) must be nulled by onCloseWindow's pre-CallAfter
    // teardown, same as their nib.ui/session/lexer/keymap siblings - otherwise a plugin's NPPN_SHUTDOWN
    // handler (delivered above, after the frame is destroyed) reaches a dangling `[this]` capture. These
    // are the same static globals main.cpp defines (this TU #includes it), so a direct read after wxEntry
    // has returned - frame long gone - both proves the null-out AND cannot itself use-after-free.
    check(!g_nibDocViewCount && !g_nibDocIdAt && !g_nibDocPosOf && !g_nibDocIndexOfActive &&
          !g_nibDocActivateAt && !g_nibDocSetLangById && !g_nibDocEncodingGet && !g_nibDocEncodingSet &&
          !g_nibDocEolGet && !g_nibDocEolSet && !g_nibDocSaveActiveAs && !g_nibDocSaveById &&
          !g_nibDocSetDirtyActive && !g_nibDocRenameUntitled && !g_nibDocTabColorId,
          "nib.documents v5 host hooks are all nulled after real shutdown (no dangling `this` capture survives)");
    std::printf(g_failCount ? "\nFAILED  (%d passed, %d failed)\n"
                            : "\nPASSED  (%d passed, %d failed)\n", g_pass, g_failCount);
    std::fflush(stdout);
    return rc != 0 ? rc : (g_failCount ? 1 : 0);
}
