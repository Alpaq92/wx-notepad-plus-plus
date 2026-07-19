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

static int g_pass = 0;
static int g_failCount = 0;
static void check(bool ok, const char* what)
{
    std::printf(ok ? "  ok    %s\n" : "  FAIL  %s\n", what);
    if (ok) ++g_pass; else ++g_failCount;
}

// ---- the sandbox (set in main() BEFORE wxEntry, read by the traits below) --------------------------
static wxString g_sandboxRoot;       // <temp>/wxnote_bridge_selftest
static wxString g_sandboxUserData;   // <root>/userdata - what the app believes its user-data dir is

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
        if (!WxnApp::OnInit()) return false;   // the REAL boot: frame + nib surface + loadNibPlugins()
        CallAfter([this] { runAll(); });       // run once the loop is live (CallAfter/event paths behave as in the app)
        return true;
    }
    wxAppTraits* CreateTraits() override { return new SandboxTraits; }

private:
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

        // ---- (c) allocator grants: inside the pools, disjoint from every host-reserved number ------
        long long okC = 0, okM = 0, okI = 0; int cFirst = -1, mFirst = -1, iFirst = -1, n = 0;
        check(parseAlloc(L, "allocCmd", okC, cFirst, n) && okC == TRUE && n == 2,
              "NPPM_ALLOCATECMDID granted 2 ids");
        check(cFirst >= NIB_ALLOC_CMD_FIRST && cFirst + 1 <= NIB_ALLOC_CMD_LAST,
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

        // ---- fixture files in the sandbox ----------------------------------------------------------
        const wxString work = g_sandboxRoot + wxFILE_SEP_PATH + "work";
        wxFileName::Mkdir(work, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
        const wxString fileA = work + wxFILE_SEP_PATH + "a.txt";
        const wxString fileB = work + wxFILE_SEP_PATH + "b.txt";
        check(writeWholeFile(fileA, "alpha\n") && writeWholeFile(fileB, "bravo\n"),
              "fixture files created in the sandbox");

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

        // ---- done ----------------------------------------------------------------------------------
        std::printf(g_failCount ? "\nFAILED  (%d passed, %d failed)\n"
                                : "\nPASSED  (%d passed, %d failed)\n", g_pass, g_failCount);
        std::fflush(stdout);
        // Force-close skips re-prompts (everything is saved) and runs the app's REAL shutdown path:
        // onCloseWindow -> toolbar teardown -> CallAfter-deferred unloadNibPlugins - the exact
        // ordering the plugin-unload-crash fix mandates, exercised once more on the way out.
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
    return rc != 0 ? rc : (g_failCount ? 1 : 0);
}
