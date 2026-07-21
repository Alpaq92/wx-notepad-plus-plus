// SPDX-License-Identifier: Apache-2.0
//
// keymap_selftest - headless self-test for the shortcut store + conflict engine (Phase 3 of
// the shortcut work). No frame, no display: it seeds a KeymapStore
// exactly as the app's Tier 0 seeding would for a handful of commands, builds the ConflictEngine over the
// static Scintilla-default mirror, and asserts the four conflict CLASSES. It exists
// because the classification hinges on wx's RUNTIME key parsing (wxAcceleratorEntry::FromString keycodes,
// the SCK_*->WXK_* remap) which a source trace cannot confirm - a real FromString run can.
//
//   cmake --build build --target keymap_selftest && build/bin/keymap_selftest
//
// Deliberately a separate target so no test-only code ships in wxnote; it links only wx core/base (for
// wxAcceleratorEntry/wxString), needs no GUI, and pulls in only the header-only keymap/conflict code.
#include "command_ids.h"
#include "keymap_store.h"
#include "keymap_schemes.h"    // Phase 2: the bundled wxnote.default preset + registerBundledScheme
#include "conflict_engine.h"
#include "shortcut_labels.h"   // Phase 4: the curated Scintilla "Editor commands" tier
// The REAL Tier-0 data: menu_builder.h's seedKeymapDefaults walks the same static menu tables the app
// builds its bar from, so the consensus-defaults section below asserts against the SHIPPED defaults
// rather than a hand-mirrored copy. menu_data_view.h expects its includer to define myID_VIEW_TERMINAL
// (main.cpp gets it from terminal_panel.h, whose vterm dependency is too heavy for this headless test).
static const int myID_VIEW_TERMINAL = 60200;   // keep in sync with terminal_panel.h:38
#include "menu_builder.h"

#include <wx/app.h>
#include <wx/accel.h>
#include <wx/file.h>
#include <wx/filename.h>
#include <wx/filefn.h>
#include <wx/log.h>
#include <cstdio>
#include <string>
#include <vector>

static int g_fail = 0;
static int g_pass = 0;
static void check(bool ok, const char* what)
{
    std::printf(ok ? "  ok    %s\n" : "  FAIL  %s\n", what);
    if (ok) ++g_pass; else ++g_fail;
}

// ---- hermetic temp-file helpers for the save/reload round-trip tests --------------------------------
// Each round-trip test needs its own throwaway userDataDir holding a shortcuts.json. Use a uniquely-named
// subdir of the OS temp dir and delete any leftover file first, so a re-run never resolves stale bindings
// (the store's load() reads "<dir>/shortcuts.json"). All I/O is wxLogNull-wrapped: a temp-dir hiccup must
// fail the assertion under test, never pop a dialog in a headless run.
static wxString makeTempSubdir(const char* leaf)
{
    const wxString dir = wxFileName::GetTempDir() + wxFILE_SEP_PATH + wxString::FromAscii(leaf);
    wxLogNull noLog;
    wxFileName::Mkdir(dir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
    const wxString f = dir + wxFILE_SEP_PATH + "shortcuts.json";
    if (wxFileExists(f)) wxRemoveFile(f);
    return dir;
}
static void writeTextFile(const wxString& path, const std::string& data)
{
    wxLogNull noLog;
    wxFile f(path, wxFile::write);
    if (f.IsOpened()) f.Write(data.data(), data.size());
}
static wxString readTextFile(const wxString& path)
{
    wxLogNull noLog;
    wxString out;
    wxFile f(path, wxFile::read);
    if (f.IsOpened()) f.ReadAll(&out, wxConvUTF8);
    return out;
}

// Mirror of WxnShellFrame::collectFrameAccels (src/main.cpp): that method is frame-bound (its translation
// unit carries wxIMPLEMENT_APP and cannot be linked into a standalone test), so we replicate its EXACT
// filtering against a fixed menu-command list to prove the two dispatch configurations we require.
// Given a scope, it yields the wxAcceleratorEntry list the frame would install:
//   * non-terminal scope -> every plain (non-chord) Global accel of every listed command (the BORDERLESS
//     frame's own table, and the exact entries the NATIVE menubar rebuilds from the same store);
//   * terminal scope -> the EMPTY list refreshAccelerators(Scope::Terminal) installs (the Phase 0 gate).
// We stop at the wxAcceleratorEntry vector rather than a wxAcceleratorTable so nothing here needs a GUI.
static std::vector<wxAcceleratorEntry> buildFrameAccelTable(const KeymapStore& store,
                                                            const std::vector<int>& menuCmdIds,
                                                            bool terminalScope)
{
    std::vector<wxAcceleratorEntry> out;
    if (terminalScope) return out;   // Scope::Terminal leaves the frame table empty (see refreshAccelerators)
    for (int id : menuCmdIds)
    {
        const EffectiveBinding* b = store.effectiveByCmd(id);
        if (!b) continue;
        for (const EffectiveAccel& a : b->accels)
        {
            if (a.isChord || a.scope != KeyScope::Global || a.raw.empty()) continue;
            wxAcceleratorEntry e;
            if (e.FromString(a.raw)) out.push_back(wxAcceleratorEntry(e.GetFlags(), e.GetKeyCode(), id));
        }
    }
    return out;
}

// A command whose Ctrl+Shift+T default shadows SCI_LINECOPY (the one day-one casualty).
static const char* SYM_RESTORE = "file.restoreLastClosed";
static const char* SYM_COPY    = "edit.copy";
static const char* SYM_LINEUP  = "edit.lineOperations.moveUp";
static const char* SYM_SAVE    = "file.save";
static const char* SYM_PRINT   = "file.print";

// ---- a SYNTHETIC bundled preset for the scheme-mechanics tests --------------------------------------
// The app ships only the "wxnote.default" identity preset (the compiled-in "Notepad++" preset was
// removed - N++ keys arrive solely via the npp-shortcuts-compat shortcuts.xml import). The scheme
// MECHANICS the removed preset used to exercise (a delta-table bind + unbind, live scheme switching,
// the read-only bundled guard) are provider-independent, so a synthetic preset with an equivalent shape
// drives them: it BINDS single-line comment to Ctrl+K and UNBINDS the Ctrl+1 tab-switch.
static constexpr SchemeDelta kTestPresetDeltas[] = {
    { "edit.commentUncomment.setSingle", "Ctrl+K" },
    { "view.tab.tab1",                   nullptr  },
};
static const char* TEST_PRESET_ID = "test.preset";
static void registerTestPreset(KeymapStore& store)
{
    registerBundledScheme(store, TEST_PRESET_ID, "Test preset", "wxnote.default",
                          kTestPresetDeltas, WXSIZEOF(kTestPresetDeltas));
}

class SelfTestApp : public wxApp
{
public:
    // Mirror terminal_selftest's exit contract: return true and run once the loop is live, so ExitMainLoop
    // gives a clean process exit code (0). Pass/fail is reported on stdout (PASSED / FAILED n), matching the
    // sibling harness - the checks here need only wx initialized (wxAcceleratorEntry), no event loop.
    bool OnInit() override
    {
        CallAfter([this]{ runAll(); ExitMainLoop(); });
        return true;
    }

    void runAll()
    {
        std::printf("keymap_selftest\n");

        // ---- NormKey sanity: the wx/Scintilla key spaces must reconcile ----------------------------
        {
            const NormKey t = conflictKeys::fromAccelString("Ctrl+Shift+T");
            check(t.key == 'T', "FromString(Ctrl+Shift+T) keycode is 'T'");
            check((t.mods & kModCtrl) && (t.mods & kModShift) && !(t.mods & kModAlt), "Ctrl+Shift+T mods are Ctrl+Shift");

            const NormKey del  = conflictKeys::fromAccelString("Del");
            const NormKey sdel = conflictKeys::fromScintilla(SCK_DELETE, SCMOD_NORM);
            check(SCK_DELETE != WXK_DELETE, "SCK_DELETE and WXK_DELETE differ (remap is load-bearing)");
            check(del == sdel, "menu 'Del' and Scintilla SCK_DELETE normalize to the same NormKey");

            const NormKey esc  = conflictKeys::fromScintilla(SCK_ESCAPE, SCMOD_NORM);
            check(esc.key == WXK_ESCAPE, "SCK_ESCAPE remaps to WXK_ESCAPE (7 -> 27)");
        }

        // ---- seed a store with just the commands the class assertions need --------------------------
        KeymapStore store;
        store.addDefault(SYM_COPY,    kCmdEditCopy,                 "Ctrl+C");        // benign vs SCI_COPY
        store.addDefault(SYM_RESTORE, kCmdFileRestoreLastClosedFile, "Ctrl+Shift+T"); // shadows SCI_LINECOPY
        store.addDefault(SYM_LINEUP,  kCmdEditLineUp,               "Ctrl+Shift+Up"); // vs terminal scoped chord
        store.addDefault(SYM_SAVE,    kCmdFileSave,                 "Ctrl+S");
        store.addDefault(SYM_PRINT,   kCmdFilePrint,                "Ctrl+P");
        store.resolveAll();

        ConflictEngine eng;
        eng.rebuild(store, [](int){ return wxString(); });

        // ---- class: BENIGN SHADOW (Ctrl+C over SCI_COPY) -> not a warning --------------------------
        {
            const ConflictInfo ci = eng.forCommand(kCmdEditCopy);
            check(ci.cls == ConflictClass::BenignShadow, "Ctrl+C over SCI_COPY classifies BENIGN");
            check(!ci.isWarning(), "Ctrl+C benign shadow raises no warning");
        }

        // ---- class: NON-EQUIVALENT SHADOW (Ctrl+Shift+T over SCI_LINECOPY) -> warn ------------------
        {
            const ConflictInfo ci = eng.forCommand(kCmdFileRestoreLastClosedFile);
            check(ci.cls == ConflictClass::NonEquivShadow, "Ctrl+Shift+T over SCI_LINECOPY classifies NON-EQUIV SHADOW");
            check(ci.isWarning(), "Ctrl+Shift+T shadow is a warning");
            check(ci.otherSciName == "SCI_LINECOPY", "shadow names the shadowed editor command SCI_LINECOPY");
        }

        // ---- class: SCOPED-OK (menu Ctrl+Shift+Up vs terminal Ctrl+Shift+Up) -> not a warning -------
        {
            const ConflictInfo ci = eng.forCommand(kCmdEditLineUp);
            check(ci.cls == ConflictClass::ScopedOk, "menu Ctrl+Shift+Up vs terminal chord classifies SCOPED-OK");
            check(!ci.isWarning(), "scoped-ok coexistence raises no warning");
        }

        // ---- class: HARD (proposing an already-used key onto a second command) ---------------------
        {
            const NormKey ctrlS = conflictKeys::fromAccelString("Ctrl+S");   // owned by kCmdFileSave
            const ConflictInfo ci = eng.forProposed(kCmdFilePrint, ctrlS, KeyScope::Global);
            check(ci.cls == ConflictClass::Hard, "binding Ctrl+S onto a second command is a HARD conflict");
            check(ci.otherCmdId == kCmdFileSave, "HARD conflict names the owning command id");
        }

        // ---- proposing a FREE key -> no conflict ---------------------------------------------------
        {
            const NormKey f9 = conflictKeys::fromAccelString("F9");
            const ConflictInfo ci = eng.forProposed(kCmdFilePrint, f9, KeyScope::Global);
            check(ci.cls == ConflictClass::None, "a free key (F9) proposes with no conflict");
        }

        // ---- store edit paths the mapper drives (rebind / unbind / resetToDefault) ------------------
        {
            store.rebind(SYM_SAVE, "Ctrl+Alt+S");
            const EffectiveBinding* b = store.effective(SYM_SAVE);
            check(b && b->primaryRaw().Lower().Contains("alt"), "rebind moves Save to Ctrl+Alt+S (user layer)");
            check(b && b->source == BindingSource::User, "rebound Save reports source User");

            store.resetToDefault(SYM_SAVE);
            const EffectiveBinding* b2 = store.effective(SYM_SAVE);
            check(b2 && b2->primaryRaw() == "Ctrl+S", "resetToDefault restores the Tier 0 default Ctrl+S");
            check(b2 && b2->source == BindingSource::Default, "reset Save reports source Default");

            store.unbind(SYM_PRINT);
            const EffectiveBinding* b3 = store.effective(SYM_PRINT);
            check(b3 && b3->accels.empty(), "unbind (Clear) drops Print's accelerator");
        }

        // ---- Phase 4: the Scintilla editor tier -------------------
        {
            // Every curated default accel must round-trip wx<->Scintilla, or applyEditorKeymap would clear
            // a default key it can't re-assign. Only a real FromString/ToRawString + SCK-translation run
            // proves it (a source trace cannot). Since the consensus adoption a row's curated default may
            // DIVERGE from its stock key (or be empty = unbound-by-default): the STOCK pair must always
            // translate (computeEditorOps vacates through it), an unbound default must derive an EMPTY
            // accel, and every bound default must land on the same NormKey through both the accel-string
            // and the Scintilla translation.
            bool allRoundTrip = true;
            const char* firstBad = "";
            for (size_t i = 0; i < kEditorCommandCount; ++i)
            {
                const EditorCommandDef& d = kEditorCommands[i];
                const wxString raw = editorDefaultAccelRaw(d);
                const NormKey stock = conflictKeys::fromScintilla(sciKeyFromWx(d.wxKey), sciModsFromWx(d.wxMods));
                if (!stock.valid()) { allRoundTrip = false; firstBad = d.name; break; }
                const int defKey = editorDefaultWxKey(d);
                if (defKey == 0)
                {
                    if (!raw.empty()) { allRoundTrip = false; firstBad = d.name; break; }
                    continue;
                }
                wxAcceleratorEntry e;
                const NormKey a = conflictKeys::fromAccelString(raw);
                const NormKey b = conflictKeys::fromScintilla(sciKeyFromWx(defKey), sciModsFromWx(editorDefaultWxMods(d)));
                if (raw.empty() || !e.FromString(raw) || !a.valid() || !(a == b)) { allRoundTrip = false; firstBad = d.name; break; }
            }
            check(allRoundTrip, "every curated editor default accel round-trips wx<->Scintilla");
            if (!allRoundTrip) std::printf("        first failing editor command: %s\n", firstBad);

            KeymapStore es;
            seedEditorKeymapDefaults(es);
            es.resolveAll();   // seeding no longer resolves per-row (mirrors menu addDefault); one resolve after
            check(es.editorAll().size() == kEditorCommandCount, "editor tier seeds all curated commands");

            const EditorEffective* lc = es.editorEffective("editor.lineCut");
            check(lc && lc->effectiveRaw == "Ctrl+L" && !lc->overridden, "editor.lineCut default resolves to Ctrl+L");

            es.setEditorBinding("editor.lineCut", "Ctrl+K");
            lc = es.editorEffective("editor.lineCut");
            check(lc && lc->overridden && lc->effectiveRaw.Lower().Contains("ctrl+k"), "setEditorBinding moves editor.lineCut to Ctrl+K");

            es.resetEditorToDefault("editor.lineCut");
            lc = es.editorEffective("editor.lineCut");
            check(lc && !lc->overridden && lc->effectiveRaw == "Ctrl+L", "resetEditorToDefault restores editor.lineCut default");

            es.setEditorBinding("editor.lineCut", wxString());   // explicit clear
            lc = es.editorEffective("editor.lineCut");
            check(lc && lc->overridden && lc->effectiveRaw.empty(), "clearing editor.lineCut leaves it unbound");

            // The historic editor.lineCopy/Ctrl+Shift+T shadow is RESOLVED in the data now: the curated
            // default is cleared (File > Restore owns the chord per the reopen-closed-tab consensus), so
            // the editor row raises nothing - it is unbound, not silently dead. The static-mirror shadow
            // on the MENU row (SCI_LINECOPY, asserted earlier) remains the expected warn-only class.
            KeymapStore cs;
            seedEditorKeymapDefaults(cs);
            cs.addDefault(SYM_RESTORE, kCmdFileRestoreLastClosedFile, "Ctrl+Shift+T");
            cs.resolveAll();
            const EditorEffective* lcopy = cs.editorEffective("editor.lineCopy");
            check(lcopy && lcopy->effectiveRaw.empty() && !lcopy->overridden,
                  "editor.lineCopy is unbound-by-default (remappable, not shadow-dead)");
            ConflictEngine ceng;
            ceng.rebuild(cs, [](int){ return wxString(); });
            check(ceng.forEditor("editor.lineCopy").cls == ConflictClass::None,
                  "editor.lineCopy no longer shadows Ctrl+Shift+T (no conflict on the row)");
            check(!ceng.forEditor("editor.wordLeft").isWarning(),
                  "editor.wordLeft at its default raises no warning");

            // Proposed editor rebinds: onto another editor command's key is HARD; onto a menu accelerator's
            // key the menu shadows it (NON-EQUIV), since the frame accel fires first.
            KeymapStore ps;
            seedEditorKeymapDefaults(ps);
            ps.addDefault(SYM_SAVE, kCmdFileSave, "Ctrl+S");
            ps.resolveAll();
            ConflictEngine peng;
            peng.rebuild(ps, [](int){ return wxString(); });
            const NormKey ctrlLeft = conflictKeys::fromAccelString("Ctrl+Left");   // editor.wordLeft's key
            check(peng.forProposedEditor("editor.lineCut", SCI_LINECUT, ctrlLeft).cls == ConflictClass::Hard,
                  "moving an editor command onto another editor command's key is HARD");
            const NormKey ctrlS = conflictKeys::fromAccelString("Ctrl+S");
            check(peng.forProposedEditor("editor.lineCut", SCI_LINECUT, ctrlS).cls == ConflictClass::NonEquivShadow,
                  "moving an editor command onto a menu key -> shadowed (NON-EQUIV)");
        }

        // ---- the 6-editor consensus defaults (v0.8.6): asserted against the REAL Tier-0 data ---------
        // seedKeymapDefaults walks the SAME static menu tables the app builds its bar from, so these
        // checks pin the SHIPPED defaults: the headline Ctrl+/ adoption, the dual-accel redo/close rows,
        // the Save As / Save All cascade, the Ctrl+D fork's modal resolution, the resolved lineCopy
        // shadow - and that the whole new default set carries no HARD conflict anywhere (shadows over
        // Scintilla's static mirror are expected, warn-only classes).
        {
            // Every newly-adopted punctuation/named chord must be expressible by THIS wx build's
            // accelerator parser (the reason hand-typed "Ctrl+[" spellings were historically avoided in
            // the editor tier) - only a runtime FromString proves it. Ctrl+` in particular was adopted
            // conditionally on this very check.
            bool allParse = true;
            const char* badSpell = "";
            for (const char* spell : { "Ctrl+/", "Ctrl+`", "Ctrl+[", "Ctrl+]", "Ctrl+=", "Ctrl+-",
                                       "Ctrl+Enter", "Ctrl+Shift+Z", "Ctrl+F4", "Ctrl+Shift+D", "Ctrl+Shift+K" })
            {
                wxAcceleratorEntry e;
                if (!e.FromString(spell)) { allParse = false; badSpell = spell; break; }
            }
            check(allParse, "every adopted consensus chord parses with wxAcceleratorEntry::FromString");
            if (!allParse) std::printf("        first unparsable chord: %s\n", badSpell);

            KeymapStore real;
            seedKeymapDefaults(real);          // the app's actual menu-tier Tier 0 (menu_builder.h)
            seedEditorKeymapDefaults(real);    // + the curated editor tier (shortcut_labels.h)
            registerKeymapSchemes(real);
            real.resolveAll();

            auto prim = [&](const char* sym) -> wxString {
                const EffectiveBinding* b = real.effective(sym);
                return b ? b->primaryRaw() : wxString("<missing>");
            };
            auto hasAccel = [&](const char* sym, const char* canon) {
                const EffectiveBinding* b = real.effective(sym);
                if (!b) return false;
                for (const EffectiveAccel& a : b->accels)
                    if (keySpell::canonical(a.raw) == canon) return true;
                return false;
            };

            ConflictEngine reng;
            reng.rebuild(real, [](int){ return wxString(); });

            // THE headline adoption: Ctrl+/ toggle line comment (the one 6/6-unanimous non-CUA chord).
            check(prim("edit.commentUncomment.toggleSingle") == "Ctrl+/",
                  "toggle line comment defaults to Ctrl+/");
            const std::vector<int> slash = reng.menuCommandsBoundTo(conflictKeys::fromAccelString("Ctrl+/"));
            check(slash.size() == 1 && slash[0] == kCmdEditBlockComment,
                  "Ctrl+/ resolves to exactly the comment command");
            // The stock SCI_WORDPARTLEFT mirror row on Ctrl+/ is VACATED at runtime (the curated
            // editor.wordPartLeft default is cleared, so computeEditorOps clears the stock key on the
            // live STCs) - the engine excludes vacated mirror rows, so the menu chord raises NOTHING.
            // The former warn-only shadow here is exactly what the fresh-install zero-issue contract
            // (the INVARIANT block below) forbids.
            check(reng.forCommand(kCmdEditBlockComment).cls == ConflictClass::None,
                  "Ctrl+/ raises no issue: the vacated stock SCI_WORDPARTLEFT row is not an owner");

            // Redo carries BOTH accels (Ctrl+Shift+Z is the only redo chord bound in all 6 editors);
            // the menu label shows the primary only.
            check(prim("edit.redo") == "Ctrl+Y", "redo's primary (menu label) stays Ctrl+Y");
            check(hasAccel("edit.redo", "ctrl+y") && hasAccel("edit.redo", "ctrl+shift+z"),
                  "redo carries both Ctrl+Y and Ctrl+Shift+Z");

            // ...and the NATIVE frame path must actually install those secondaries: the native menubar
            // derives exactly ONE accelerator per item from the "\t<primary>" label suffix, so
            // rewriteMenuAccelLabels (main.cpp) feeds EffectiveBinding::secondaryRaws() to
            // wxMenuItem::AddExtraAccel. Assert the SHARED derivation (same member the frame calls)
            // yields exactly the secondary chord for both dual-default rows, and that each parses into
            // the installable wxAcceleratorEntry AddExtraAccel needs - store-resolved-but-never-firing
            // is precisely the regression this guards.
            auto nativeExtras = [&](const char* sym) -> std::vector<wxAcceleratorEntry> {
                std::vector<wxAcceleratorEntry> out;
                const EffectiveBinding* b = real.effective(sym);
                if (!b) return out;
                for (const wxString& rawSpell : b->secondaryRaws())
                {
                    wxAcceleratorEntry e;
                    if (e.FromString(rawSpell)) out.push_back(e);
                }
                return out;
            };
            const std::vector<wxAcceleratorEntry> redoExtras = nativeExtras("edit.redo");
            check(redoExtras.size() == 1 && redoExtras[0].GetKeyCode() == 'Z'
                      && redoExtras[0].GetFlags() == (wxACCEL_CTRL | wxACCEL_SHIFT),
                  "native path: redo's Ctrl+Shift+Z derives as an installable extra accel");
            const std::vector<wxAcceleratorEntry> closeExtras = nativeExtras("file.close");
            check(closeExtras.size() == 1 && closeExtras[0].GetKeyCode() == WXK_F4
                      && closeExtras[0].GetFlags() == wxACCEL_CTRL,
                  "native path: close's Ctrl+F4 derives as an installable extra accel");

            // Close tab: Ctrl+W primary + Ctrl+F4 secondary (each live in 4-5 of the 6 editors).
            check(prim("file.close") == "Ctrl+W" && hasAccel("file.close", "ctrl+f4"),
                  "close carries both Ctrl+W and Ctrl+F4 (Ctrl+W on the label)");

            // The Save As cascade: Ctrl+Shift+S to Save As, Save All evicted to menu-only.
            check(prim("file.saveAs") == "Ctrl+Shift+S", "Save As = Ctrl+Shift+S");
            const EffectiveBinding* saveAll = real.effective("file.saveAll");
            check(saveAll && saveAll->accels.empty(), "Save All is unbound (menu-only, per 4 of 6 editors)");

            // The Ctrl+D fork's modal resolution.
            check(prim("edit.multiselectNext.ignoreCaseWholeWord") == "Ctrl+D",
                  "Ctrl+D = add next occurrence (multi-select)");
            check(prim("edit.lineOperations.duplicateLine") == "Ctrl+Shift+D",
                  "duplicate line moved to Ctrl+Shift+D");

            // Indent/outdent chords, insert-line-below, terminal, zoom.
            check(prim("edit.indent.increase") == "Ctrl+]" && prim("edit.indent.decrease") == "Ctrl+[",
                  "indent/outdent adopt Ctrl+] / Ctrl+[");
            check(prim("edit.lineOperations.blankLineBelowCurrent") == "Ctrl+Enter",
                  "insert blank line below = Ctrl+Enter");
            check(prim("view.terminal") == "Ctrl+`", "toggle terminal = Ctrl+`");
            check(prim("view.zoom.in") == "Ctrl+=" && prim("view.zoom.out") == "Ctrl+-",
                  "zoom in/out = Ctrl+= / Ctrl+-");
            const EffectiveBinding* zr = real.effective("view.zoom.restore");
            check(zr && zr->accels.empty(), "zoom restore stays unbound (the Ctrl+0 trap is kept out)");

            // Reopen-closed-tab keeps Ctrl+Shift+T; the lineCopy side of that shadow is resolved.
            check(prim("file.restoreLastClosed") == "Ctrl+Shift+T", "reopen closed tab keeps Ctrl+Shift+T");
            const EditorEffective* lcopy2 = real.editorEffective("editor.lineCopy");
            check(lcopy2 && lcopy2->effectiveRaw.empty()
                      && reng.forEditor("editor.lineCopy").cls == ConflictClass::None,
                  "editor.lineCopy no longer shadows Ctrl+Shift+T in the full default set");

            // Editor-tier adoptions/clears.
            const EditorEffective* ldel = real.editorEffective("editor.lineDelete");
            check(ldel && ldel->effectiveRaw == "Ctrl+Shift+K" && !ldel->overridden,
                  "delete line defaults to Ctrl+Shift+K (editor tier)");
            const EditorEffective* pup = real.editorEffective("editor.paragraphUp");
            const EditorEffective* pdn = real.editorEffective("editor.paragraphDown");
            const EditorEffective* wpl = real.editorEffective("editor.wordPartLeft");
            check(pup && pup->effectiveRaw.empty() && pdn && pdn->effectiveRaw.empty()
                      && wpl && wpl->effectiveRaw.empty(),
                  "paragraphUp/Down + wordPartLeft defaults cleared for the adopted menu chords");
            const EditorEffective* lcut = real.editorEffective("editor.lineCut");
            check(lcut && lcut->effectiveRaw == "Ctrl+L",
                  "cut line keeps Ctrl+L (consensus is empty-selection Ctrl+X behavior, no chord to adopt)");

            // Research-flagged traps stay untouched.
            check(prim("edit.convertCaseTo.lowercase") == "Ctrl+U"
                      && prim("edit.convertCaseTo.uppercase") == "Ctrl+Shift+U"
                      && prim("view.tab.tab1") == "Ctrl+1"
                      && prim("view.foldLevel.1") == "Alt+1"
                      && prim("edit.lineOperations.moveUp") == "Ctrl+Shift+Up",
                  "trap rows keep current bindings (Ctrl+U case, Ctrl+digit tabs, Alt+digit folds, move line)");

            // NO HARD conflict anywhere in the shipped default set - menu commands and editor rows alike.
            bool anyHard = false;
            wxString hardWho;
            for (const EffectiveBinding* b : real.all())
            {
                if (!b || b->cmdId == 0) continue;
                if (reng.forCommand(b->cmdId).cls == ConflictClass::Hard) { anyHard = true; hardWho = b->symbolicName; break; }
            }
            if (!anyHard)
                for (const EditorEffective& e : real.editorAll())
                    if (reng.forEditor(e.name).cls == ConflictClass::Hard) { anyHard = true; hardWho = e.name; break; }
            check(!anyHard, "the shipped default set carries NO HARD conflicts");
            if (anyHard) std::printf("        HARD conflict on: %s\n", std::string(hardWho.utf8_str()).c_str());

            // A mapper rebind still replaces BOTH of a dual-accel row's defaults with the one new key
            // (pushReplace's bare unbind wipes all inherited accels, secondaries included).
            real.rebind("edit.redo", "Ctrl+R");
            const EffectiveBinding* rr = real.effective("edit.redo");
            check(rr && rr->accels.size() == 1 && keySpell::canonical(rr->primaryRaw()) == "ctrl+r",
                  "rebinding redo replaces both default accels with the single new key");
            check(nativeExtras("edit.redo").empty(),
                  "...and the native extra-accel list empties with it (no stale Ctrl+Shift+Z)");
        }

        // ---- THE INVARIANT: the SHIPPED default keymap is conflict-free -----------------------------
        // The user-facing contract: the Shortcut Mapper's details report must be literally empty ("Brak
        // konfliktow." / "No conflicts.") on a fresh install. Seed the store EXACTLY as
        // WxnShellFrameT::buildMenuBar does (menu Tier 0 + curated editor tier + bundled schemes; a fresh
        // install has no shortcuts.json, so no load()), build the ConflictEngine the same way the mapper
        // does, and require allIssues() empty - printing every offending row so a regression names itself.
        {
            KeymapStore fresh;
            seedKeymapDefaults(fresh);         // buildMenuBar order: menu Tier 0...
            seedEditorKeymapDefaults(fresh);   // ...the curated editor tier...
            registerKeymapSchemes(fresh);      // ...the bundled presets; load() skipped = fresh install
            fresh.resolveAll();
            ConflictEngine feng;
            feng.rebuild(fresh, [](int){ return wxString(); });

            const std::vector<ConflictIssue> issues = feng.allIssues();
            check(issues.empty(), "INVARIANT: pure defaults produce ZERO mapper issues (allIssues empty)");
            for (const ConflictIssue& is : issues)
            {
                std::printf("        issue %s [%s]\n",
                            std::string(is.keyDisplay.utf8_str()).c_str(),
                            is.cls == ConflictClass::Hard ? "HARD" : "NON-EQUIV SHADOW");
                for (const KeyOwner& o : is.owners)
                    std::printf("          owner kind=%d scope=%d cmdId=%d sciCmd=%d name=%s\n",
                                (int)o.kind, (int)o.scope, o.cmdId, o.sciCmd,
                                std::string(o.name.utf8_str()).c_str());
            }

            // ...and the engine's SciDefault exclusion cannot drift from the runtime keymap: every stock
            // key it stops counting must ACTUALLY be vacated in computeEditorOps' op list (the same list
            // reapplyEditorKeymaps applies to the live STCs). Pin the full expected vacate set at the
            // defaults: the curated divergences plus every kStockVacated row.
            const std::vector<EditorOp> ops = computeEditorOps(fresh);
            auto vacates = [&ops](int wxKey, int wxMods, int sciCmd) {
                const int k = sciKeyFromWx(wxKey), m = sciModsFromWx(wxMods);
                for (const EditorOp& op : ops)
                    if (op.defKey == k && op.defMods == m && op.sciCmd == sciCmd) return true;
                return false;
            };
            check(vacates('/', wxACCEL_CTRL, SCI_WORDPARTLEFT),
                  "defaults vacate stock Ctrl+/ (SCI_WORDPARTLEFT -> menu comment toggle)");
            check(vacates('[', wxACCEL_CTRL, SCI_PARAUP),
                  "defaults vacate stock Ctrl+[ (SCI_PARAUP -> menu outdent)");
            check(vacates(']', wxACCEL_CTRL, SCI_PARADOWN),
                  "defaults vacate stock Ctrl+] (SCI_PARADOWN -> menu indent)");
            check(vacates('T', wxACCEL_CTRL | wxACCEL_SHIFT, SCI_LINECOPY),
                  "defaults vacate stock Ctrl+Shift+T (SCI_LINECOPY -> menu reopen closed tab)");
            check(vacates('L', wxACCEL_CTRL | wxACCEL_SHIFT, SCI_LINEDELETE),
                  "defaults vacate stock Ctrl+Shift+L (SCI_LINEDELETE moved to Ctrl+Shift+K)");
            bool allStatic = true;
            const char* badRow = "";
            for (const StockVacatedKey& v : scintKeymap::kStockVacated)
            {
                bool found = false;
                for (const EditorOp& op : ops)
                    if (op.defKey == v.key && op.defMods == v.mods && op.sciCmd == v.cmd && !op.hasOverride)
                        { found = true; break; }
                if (!found) { allStatic = false; badRow = v.name; break; }
            }
            check(allStatic, "every kStockVacated row is a real vacate op (Ctrl+D/SCI_SELECTIONDUPLICATE)");
            if (!allStatic) std::printf("        missing vacate op for: %s\n", badRow);
        }

        // ---- rebind round-trips through shortcuts.json ----------------------
        // A rebind must survive save() -> a fresh load(): the store persists the portable ToRawString
        // spelling and re-resolves it, so a real FromString/ToRawString file round-trip is what proves it
        // (a source trace of serialize() cannot show the reparse succeeds).
        {
            const wxString dir = makeTempSubdir("wxnote_km_roundtrip");

            KeymapStore w;
            w.addDefault(SYM_SAVE,  kCmdFileSave,  "Ctrl+S");
            w.addDefault(SYM_PRINT, kCmdFilePrint, "Ctrl+P");
            w.load(dir);                       // no file yet -> Tier-0 defaults; also sets the save path
            w.rebind(SYM_SAVE, "Ctrl+Alt+S");  // -> user layer (unbind-then-bind)
            w.unbind(SYM_PRINT);               // -> full unbind (serialized as "-file.print")
            check(w.save(), "save() writes shortcuts.json");

            // On disk: the canonical lowercase spelling and the '-' unbind syntax.
            const wxString disk = readTextFile(dir + wxFILE_SEP_PATH + "shortcuts.json");
            check(disk.Lower().Contains("ctrl+alt+s"), "shortcuts.json stores the rebind in canonical lowercase");
            check(disk.Contains("\"-file.print\""),   "shortcuts.json stores the unbind as \"-file.print\"");

            // A fresh store with the SAME Tier-0 defaults must resolve to the persisted bindings.
            KeymapStore r;
            r.addDefault(SYM_SAVE,  kCmdFileSave,  "Ctrl+S");
            r.addDefault(SYM_PRINT, kCmdFilePrint, "Ctrl+P");
            r.load(dir);
            const EffectiveBinding* rs = r.effective(SYM_SAVE);
            check(rs && keySpell::canonical(rs->primaryRaw()) == "ctrl+alt+s" && rs->source == BindingSource::User,
                  "reloaded store: Save round-trips to Ctrl+Alt+S (user source)");
            const EffectiveBinding* rp = r.effective(SYM_PRINT);
            check(rp && rp->accels.empty(),
                  "reloaded store: the -file.print unbind removed the inherited Ctrl+P");
        }

        // ---- the "-command" unbind syntax removes an inherited accel ---------------------
        // Independent of save()'s exact output: hand-author a shortcuts.json (the file a user edits) and
        // prove the LEADING '-' on "command" is what drops the inherited Tier-0 default, while a sibling
        // rebind entry still applies.
        {
            const wxString dir = makeTempSubdir("wxnote_km_unbind");
            writeTextFile(dir + wxFILE_SEP_PATH + "shortcuts.json",
                "{\n"
                "  \"version\": 1,\n"
                "  \"userKeybindings\": [\n"
                "    { \"command\": \"-file.print\" },\n"
                "    { \"command\": \"file.save\", \"key\": \"ctrl+alt+s\" }\n"
                "  ]\n"
                "}\n");
            KeymapStore h;
            h.addDefault(SYM_SAVE,  kCmdFileSave,  "Ctrl+S");
            h.addDefault(SYM_PRINT, kCmdFilePrint, "Ctrl+P");
            h.load(dir);
            const EffectiveBinding* hp = h.effective(SYM_PRINT);
            check(hp && hp->accels.empty(), "hand-authored \"-file.print\" drops the inherited Ctrl+P default");
            // A BARE bind entry (no leading '-') ADDS a second accel rather than replacing the inherited one
            // (multiple non-unbind entries = multiple accels); only the mapper's rebind() writes
            // unbind-then-bind for a clean single-key replace. So file.save now answers to BOTH keys.
            const EffectiveBinding* hs = h.effective(SYM_SAVE);
            bool hasAltS = false, hasS = false;
            if (hs)
                for (const EffectiveAccel& a : hs->accels)
                {
                    const wxString c = keySpell::canonical(a.raw);
                    if (c == "ctrl+alt+s") hasAltS = true;
                    if (c == "ctrl+s")     hasS = true;
                }
            check(hs && hasAltS && hasS,
                  "a bare file.save bind ADDS ctrl+alt+s alongside the inherited Ctrl+S (multi-accel, plan 4.2)");
            // ...and the ADDed key must surface on the NATIVE frame path too: user multi-accel rows ride
            // the same secondaryRaws() -> AddExtraAccel pipe as the shipped dual defaults (the label
            // still shows only the primary Ctrl+S).
            const std::vector<wxString> userExtras = hs ? hs->secondaryRaws() : std::vector<wxString>();
            wxAcceleratorEntry uxe;
            check(hs && hs->primaryRaw() == "Ctrl+S" && userExtras.size() == 1
                      && keySpell::canonical(userExtras[0]) == "ctrl+alt+s" && uxe.FromString(userExtras[0]),
                  "the user-ADDed accel derives as a native extra accel (label keeps the primary)");
        }

        // ---- switching the active scheme changes a known delta (Phase 2 mechanics) ------------------
        // Driven by the synthetic bundled preset (see kTestPresetDeltas): it BINDS single-line comment to
        // Ctrl+K and UNBINDS the Ctrl+1 tab-switch. Switching the active scheme must re-resolve those
        // live, while a floating user override survives the switch (VS Code semantics).
        {
            KeymapStore ss;
            ss.addDefault("view.tab.tab1",                   kCmdViewTab1,           "Ctrl+1");   // wxNote binds Ctrl+1
            ss.addDefault("edit.commentUncomment.setSingle", kCmdEditBlockCommentSet, wxString()); // wxNote: unbound
            ss.addDefault(SYM_SAVE,                          kCmdFileSave,           "Ctrl+S");   // identical in both
            registerKeymapSchemes(ss);        // wxnote.default (identity)
            registerTestPreset(ss);           // + the synthetic delta preset
            ss.resolveAll();

            check(ss.activeScheme() == "wxnote.default", "store starts on the wxNote scheme");
            const EffectiveBinding* tab1 = ss.effective("view.tab.tab1");
            check(tab1 && tab1->primaryRaw() == "Ctrl+1", "default scheme: tab1 is Ctrl+1");
            const EffectiveBinding* setSingle = ss.effective("edit.commentUncomment.setSingle");
            check(setSingle && setSingle->accels.empty(), "default scheme: single-line comment is unbound");

            // A user override in the floating user layer must persist across the scheme switch.
            ss.rebind(SYM_SAVE, "Ctrl+Alt+S");

            ss.setActiveScheme(TEST_PRESET_ID);
            tab1 = ss.effective("view.tab.tab1");
            check(tab1 && tab1->accels.empty(), "test preset: Ctrl+1 tab-switch is unbound (preset delta)");
            setSingle = ss.effective("edit.commentUncomment.setSingle");
            check(setSingle && setSingle->primaryRaw() == "Ctrl+K", "test preset: single-line comment becomes Ctrl+K");
            check(setSingle && setSingle->source == BindingSource::Scheme, "preset delta reports source Scheme");
            const EffectiveBinding* saveB = ss.effective(SYM_SAVE);
            check(saveB && keySpell::canonical(saveB->primaryRaw()) == "ctrl+alt+s" && saveB->source == BindingSource::User,
                  "user override (Save = Ctrl+Alt+S) survives the scheme switch");

            ss.setActiveScheme("wxnote.default");
            tab1 = ss.effective("view.tab.tab1");
            check(tab1 && tab1->primaryRaw() == "Ctrl+1", "switching back to default restores Ctrl+1 tab-switch");
            setSingle = ss.effective("edit.commentUncomment.setSingle");
            check(setSingle && setSingle->accels.empty(), "switching back leaves single-line comment unbound again");
        }

        // ---- scheme-scoped EDITOR deltas: commit-inactive -> persist -> activate applies ------------
        // The nib.keymap commit path (npp-shortcuts-compat import) registers a scheme WITHOUT
        // activating it. Its editor (ScintillaKeys) binds now live IN the scheme (editorDeltas), so
        // they must (a) stay inert while the scheme is inactive, (b) survive save() -> a fresh load(),
        // and (c) apply when the scheme is later activated in the mapper - the import report's
        // "select it there to apply them" promise, previously false for the editor tier.
        {
            const wxString dir = makeTempSubdir("wxnote_km_schemeeditor");

            KeymapStore w;
            seedEditorKeymapDefaults(w);
            w.addDefault(SYM_SAVE, kCmdFileSave, "Ctrl+S");
            w.load(dir);                       // no file yet -> defaults; sets the save path + resolves

            KeymapScheme imp;                  // mirror of what g_nibKmCommit builds from a builder
            imp.id = "test.imported"; imp.name = "Imported"; imp.bundled = false;
            KeymapDelta ed;
            ed.symbolicName = "editor.lineCut"; ed.key = "Ctrl+K"; ed.scope = KeyScope::Editor;
            imp.editorDeltas.push_back(ed);
            check(w.registerScheme(imp), "registerScheme accepts a non-bundled scheme carrying editor deltas");
            w.resolveAll();                    // what the commit path runs for an activate=0 commit
            const EditorEffective* lc = w.editorEffective("editor.lineCut");
            check(lc && !lc->overridden && lc->effectiveRaw == "Ctrl+L",
                  "an INACTIVE scheme's editor delta leaves the live editor tier untouched");
            check(w.save(), "save() persists the scheme");
            const wxString disk = readTextFile(dir + wxFILE_SEP_PATH + "shortcuts.json");
            check(disk.Contains("\"editor\"") && disk.Lower().Contains("ctrl+k"),
                  "shortcuts.json carries the scheme's editor delta");

            KeymapStore r;                     // a fresh app run
            seedEditorKeymapDefaults(r);
            r.addDefault(SYM_SAVE, kCmdFileSave, "Ctrl+S");
            r.load(dir);
            const EditorEffective* rl = r.editorEffective("editor.lineCut");
            check(rl && rl->effectiveRaw == "Ctrl+L", "reloaded but not activated: editor default still resolves");
            r.setActiveScheme("test.imported");
            rl = r.editorEffective("editor.lineCut");
            check(rl && rl->overridden && keySpell::canonical(rl->effectiveRaw) == "ctrl+k",
                  "activating the scheme applies its persisted editor delta");
            r.setEditorBinding("editor.lineCut", "Ctrl+J");
            rl = r.editorEffective("editor.lineCut");
            check(rl && keySpell::canonical(rl->effectiveRaw) == "ctrl+j",
                  "the flat user layer still beats the active scheme's editor delta");
            r.resetEditorToDefault("editor.lineCut");
            rl = r.editorEffective("editor.lineCut");
            check(rl && keySpell::canonical(rl->effectiveRaw) == "ctrl+k",
                  "dropping the user override falls back to the scheme's editor delta");
            r.setActiveScheme("wxnote.default");
            rl = r.editorEffective("editor.lineCut");
            check(rl && !rl->overridden && rl->effectiveRaw == "Ctrl+L",
                  "switching the scheme away restores the editor default");
        }

        // ---- a non-bundled scheme must NEVER shadow a bundled preset's id ---------------------------
        // registerScheme's replace semantics + sticky bundled flag would otherwise let a plugin or a
        // hand-edited shortcuts.json swap a compiled preset's curated deltas for foreign data that can
        // neither be repaired nor re-serialized (the corruption duplicateScheme already guards against).
        {
            KeymapStore gs;
            gs.addDefault("view.tab.tab1",                   kCmdViewTab1,            "Ctrl+1");
            gs.addDefault("edit.commentUncomment.setSingle", kCmdEditBlockCommentSet, wxString());
            gs.addDefault(SYM_SAVE,                          kCmdFileSave,            "Ctrl+S");
            registerKeymapSchemes(gs);
            registerTestPreset(gs);            // the synthetic BUNDLED preset the rogue tries to shadow
            gs.resolveAll();

            KeymapScheme rogue;                // a plugin/file scheme claiming the preset's id
            rogue.id = TEST_PRESET_ID; rogue.name = "rogue"; rogue.bundled = false;
            KeymapDelta rd; rd.symbolicName = SYM_SAVE; rd.key = "Ctrl+Alt+S";
            rogue.deltas.push_back(rd);
            check(!gs.registerScheme(rogue), "registerScheme REFUSES a non-bundled scheme shadowing a bundled preset");

            KeymapScheme rogueDef;
            rogueDef.id = "wxnote.default"; rogueDef.name = "rogue-default"; rogueDef.bundled = false;
            check(!gs.registerScheme(rogueDef), "the reserved wxnote.default id is refused too");

            gs.setActiveScheme(TEST_PRESET_ID);
            const EffectiveBinding* ssb = gs.effective("edit.commentUncomment.setSingle");
            check(ssb && ssb->primaryRaw() == "Ctrl+K", "the preset's curated Ctrl+K delta survives the attempt");
            const EffectiveBinding* svb = gs.effective(SYM_SAVE);
            check(svb && svb->primaryRaw() == "Ctrl+S", "the rogue binding was NOT merged into the preset");

            // Same guard via the parseInto writer: a hand-edited shortcuts.json shadowing the bundled id
            // is ignored wholesale; the compiled preset keeps resolving. The file's activeScheme naming
            // the (registered) preset must still apply - proving load()'s dangling-id fallback fires
            // only for a scheme that truly does not exist.
            const wxString dir = makeTempSubdir("wxnote_km_shadow");
            writeTextFile(dir + wxFILE_SEP_PATH + "shortcuts.json",
                "{\n"
                "  \"version\": 1,\n"
                "  \"activeScheme\": \"test.preset\",\n"
                "  \"schemes\": [ { \"id\": \"test.preset\", \"keybindings\": [\n"
                "    { \"command\": \"file.save\", \"key\": \"ctrl+alt+s\" } ] } ]\n"
                "}\n");
            KeymapStore hs;
            hs.addDefault("view.tab.tab1",                   kCmdViewTab1,            "Ctrl+1");
            hs.addDefault("edit.commentUncomment.setSingle", kCmdEditBlockCommentSet, wxString());
            hs.addDefault(SYM_SAVE,                          kCmdFileSave,            "Ctrl+S");
            registerKeymapSchemes(hs);
            registerTestPreset(hs);
            hs.load(dir);
            check(hs.activeScheme() == TEST_PRESET_ID, "the file's activeScheme selection still applies");
            const EffectiveBinding* t1 = hs.effective("view.tab.tab1");
            check(t1 && t1->accels.empty(), "preset delta intact after the shadowing file: Ctrl+1 unbound");
            const EffectiveBinding* sv2 = hs.effective(SYM_SAVE);
            check(sv2 && sv2->primaryRaw() == "Ctrl+S", "the file's shadowing scheme block is ignored (Save stays Ctrl+S)");
        }

        // ---- a STALE activeScheme (a scheme this build does not ship) falls back cleanly ------------
        // The migration case for the REMOVED bundled "Notepad++" preset: a live shortcuts.json can still
        // carry "activeScheme": "notepad++". load() must resolve the default chain (no crash, no empty
        // bindings), snap the dangling pointer back to wxnote.default, and a subsequent save() must
        // write a resolvable state instead of re-persisting the dangling id forever. A user-layer
        // override in the same file must keep applying (only the scheme pointer migrates).
        {
            const wxString dir = makeTempSubdir("wxnote_km_stalescheme");
            writeTextFile(dir + wxFILE_SEP_PATH + "shortcuts.json",
                "{\n"
                "  \"version\": 1,\n"
                "  \"activeScheme\": \"notepad++\",\n"
                "  \"userKeybindings\": [ { \"command\": \"-file.print\" } ]\n"
                "}\n");
            KeymapStore ms;
            ms.addDefault("view.tab.tab1", kCmdViewTab1,  "Ctrl+1");
            ms.addDefault(SYM_SAVE,        kCmdFileSave,  "Ctrl+S");
            ms.addDefault(SYM_PRINT,       kCmdFilePrint, "Ctrl+P");
            registerKeymapSchemes(ms);
            ms.load(dir);
            const EffectiveBinding* sv = ms.effective(SYM_SAVE);
            check(sv && sv->primaryRaw() == "Ctrl+S" && sv->source == BindingSource::Default,
                  "stale activeScheme: Save resolves to the Tier-0 default (no crash)");
            const EffectiveBinding* t1 = ms.effective("view.tab.tab1");
            check(t1 && t1->primaryRaw() == "Ctrl+1", "stale activeScheme: tab1 keeps its default (bindings not empty)");
            const EffectiveBinding* pr = ms.effective(SYM_PRINT);
            check(pr && pr->accels.empty(), "the file's user-layer unbind still applies alongside the fallback");
            check(ms.activeScheme() == "wxnote.default", "the dangling scheme id snaps back to wxnote.default at load");
            check(ms.save(), "save() succeeds after the fallback");
            const wxString disk = readTextFile(dir + wxFILE_SEP_PATH + "shortcuts.json");
            check(!disk.Contains("notepad++") && disk.Contains("wxnote.default"),
                  "the re-saved file carries wxnote.default, not the dangling id");

            KeymapStore r2;                    // the next launch reads a fully resolvable file
            r2.addDefault("view.tab.tab1", kCmdViewTab1,  "Ctrl+1");
            r2.addDefault(SYM_SAVE,        kCmdFileSave,  "Ctrl+S");
            r2.addDefault(SYM_PRINT,       kCmdFilePrint, "Ctrl+P");
            registerKeymapSchemes(r2);
            r2.load(dir);
            const EffectiveBinding* rsv = r2.effective(SYM_SAVE);
            const EffectiveBinding* rpr = r2.effective(SYM_PRINT);
            check(r2.activeScheme() == "wxnote.default" && rsv && rsv->primaryRaw() == "Ctrl+S"
                      && rpr && rpr->accels.empty(),
                  "the re-saved state reloads resolvable (defaults + the preserved user unbind)");
        }

        // ---- a deeply-nested shortcuts.json must FAIL CLEANLY, not overflow the stack ---------------
        // The JSON reader's object/array/value recursion is depth-capped; past the cap the parse fails
        // and load() degrades to defaults - the "a bad hand-edit must not brick startup" contract.
        // (Without the cap, the first two loads below would crash this test with a stack overflow.)
        {
            const wxString dir = makeTempSubdir("wxnote_km_deepjson");

            writeTextFile(dir + wxFILE_SEP_PATH + "shortcuts.json", std::string(100000, '['));
            KeymapStore ds;
            ds.addDefault(SYM_SAVE, kCmdFileSave, "Ctrl+S");
            ds.load(dir);
            const EffectiveBinding* db = ds.effective(SYM_SAVE);
            check(db && db->primaryRaw() == "Ctrl+S", "a 100k-deep nested file degrades to defaults (no crash)");

            // deep nesting INSIDE a member value (exercises the object -> value -> array path too)
            writeTextFile(dir + wxFILE_SEP_PATH + "shortcuts.json",
                          "{ \"version\": 1, \"userKeybindings\": " + std::string(50000, '['));
            KeymapStore ds2;
            ds2.addDefault(SYM_SAVE, kCmdFileSave, "Ctrl+S");
            ds2.load(dir);
            const EffectiveBinding* db2 = ds2.effective(SYM_SAVE);
            check(db2 && db2->primaryRaw() == "Ctrl+S", "deep nesting inside a member also fails cleanly to defaults");

            // sanity: the cap must not reject legitimately-shallow files
            writeTextFile(dir + wxFILE_SEP_PATH + "shortcuts.json",
                "{ \"version\": 1, \"userKeybindings\": [ { \"command\": \"file.save\", \"key\": \"ctrl+alt+s\" } ] }");
            KeymapStore ds3;
            ds3.addDefault(SYM_SAVE, kCmdFileSave, "Ctrl+S");
            ds3.load(dir);
            const EffectiveBinding* db3 = ds3.effective(SYM_SAVE);
            bool parsedBind = false;           // a bare bind ADDS alongside the default (see plan 4.2 above)
            if (db3)
                for (const EffectiveAccel& a : db3->accels)
                    if (keySpell::canonical(a.raw) == "ctrl+alt+s") parsedBind = true;
            check(parsedBind, "the depth cap leaves a normal shortcuts.json parsing");
        }

        // ---- refreshAccelerators produces a table for both frame configurations ------
        // The frame method is unlinkable here (see buildFrameAccelTable's note), so replicate its accel
        // production. Editor/borderless scope must yield a populated table (including the Ctrl+C that would
        // steal from the terminal); terminal scope must yield an EMPTY table (Ctrl+C reaches the terminal,
        // not SCI_COPY - the Phase 0 gate, headless twin of terminal_selftest's behavioural check); and the
        // NATIVE menubar path must derive the same accel from the same store's menu-label spelling.
        {
            KeymapStore fs;
            fs.addDefault(SYM_COPY,  kCmdEditCopy,  "Ctrl+C");
            fs.addDefault(SYM_SAVE,  kCmdFileSave,  "Ctrl+S");
            fs.addDefault(SYM_PRINT, kCmdFilePrint, "Ctrl+P");
            fs.addDefault("edit.redo", kCmdEditRedo, "Ctrl+Y");        // a dual-accel row, exactly as
            fs.addDefaultSecondary("edit.redo", "Ctrl+Shift+Z");       // menu_builder.h's kSecondaryDefaults ships it
            fs.resolveAll();
            const std::vector<int> menu = { kCmdEditCopy, kCmdFileSave, kCmdFilePrint, kCmdEditRedo };

            const std::vector<wxAcceleratorEntry> editorTable = buildFrameAccelTable(fs, menu, /*terminal=*/false);
            check(editorTable.size() == 5,
                  "editor/borderless scope: frame table carries all 5 accels of the 4 bound menu commands (secondary included)");
            bool hasCopy = false, hasRedoY = false, hasRedoZ = false;
            for (const wxAcceleratorEntry& e : editorTable)
            {
                if (e.GetCommand() == kCmdEditCopy && e.GetKeyCode() == 'C' && (e.GetFlags() & wxACCEL_CTRL))
                    hasCopy = true;
                if (e.GetCommand() == kCmdEditRedo && e.GetKeyCode() == 'Y' && e.GetFlags() == wxACCEL_CTRL)
                    hasRedoY = true;
                if (e.GetCommand() == kCmdEditRedo && e.GetKeyCode() == 'Z' && e.GetFlags() == (wxACCEL_CTRL | wxACCEL_SHIFT))
                    hasRedoZ = true;
            }
            check(hasCopy, "editor scope: table binds Ctrl+C to Edit>Copy (the accel that would steal from the terminal)");
            check(hasRedoY && hasRedoZ, "editor scope: table binds BOTH redo accels (borderless dual-accel dispatch)");

            const std::vector<wxAcceleratorEntry> termTable = buildFrameAccelTable(fs, menu, /*terminal=*/true);
            check(termTable.empty(), "terminal scope: frame table is empty (Ctrl+C reaches the terminal, not SCI_COPY)");

            // Native menubar config: same store, consumed as the menu-label accel that rewriteMenuAccelLabels
            // appends ("&Copy" + "\t" + primaryRaw()) PLUS the AddExtraAccel entries it derives from
            // secondaryRaws() - the label carries only ONE accel per item, so without the extras the
            // secondary would never fire on the native frame. Both frame paths must therefore agree on
            // the same full accel set from one store.
            const EffectiveBinding* cb = fs.effectiveByCmd(kCmdEditCopy);
            check(cb && cb->primaryRaw() == "Ctrl+C",
                  "native path: menu-label accel for Copy matches the frame table (one store, both frame paths)");
            const EffectiveBinding* rb = fs.effectiveByCmd(kCmdEditRedo);
            std::vector<wxAcceleratorEntry> extras;
            if (rb)
                for (const wxString& rawSpell : rb->secondaryRaws())
                {
                    wxAcceleratorEntry e;
                    if (e.FromString(rawSpell)) extras.push_back(e);
                }
            check(rb && rb->primaryRaw() == "Ctrl+Y" && extras.size() == 1
                      && extras[0].GetKeyCode() == 'Z' && extras[0].GetFlags() == (wxACCEL_CTRL | wxACCEL_SHIFT),
                  "native path: redo = Ctrl+Y label accel + Ctrl+Shift+Z extra accel (full parity with the frame table)");
        }

        // ---- Reassign steals ONLY the colliding key (mapper hard-conflict steal, one batched edit) --
        // The mapper's "Reassign" runs KeymapStore::reassignRebind: a KEYED unbind of the other command
        // (so any additional accels it holds survive the steal - a bare unbind used to wipe them all)
        // plus the row's clean rebind, in one store transaction. Give Print a second accel via a
        // hand-authored bare bind, then steal only its Ctrl+P for Save.
        {
            const wxString dir = makeTempSubdir("wxnote_km_reassign");
            writeTextFile(dir + wxFILE_SEP_PATH + "shortcuts.json",
                "{ \"version\": 1, \"userKeybindings\": [ { \"command\": \"file.print\", \"key\": \"ctrl+r\" } ] }");
            KeymapStore rs;
            rs.addDefault(SYM_SAVE,  kCmdFileSave,  "Ctrl+S");
            rs.addDefault(SYM_PRINT, kCmdFilePrint, "Ctrl+P");
            rs.load(dir);
            auto hasKey = [](const EffectiveBinding* b, const char* canon)
            {
                if (!b) return false;
                for (const EffectiveAccel& a : b->accels)
                    if (keySpell::canonical(a.raw) == canon) return true;
                return false;
            };
            const EffectiveBinding* p0 = rs.effective(SYM_PRINT);
            check(hasKey(p0, "ctrl+p") && hasKey(p0, "ctrl+r"), "pre-steal: Print answers to Ctrl+P AND Ctrl+R");

            rs.reassignRebind(SYM_PRINT, SYM_SAVE, "Ctrl+P");   // the mapper's Reassign choice
            const EffectiveBinding* p1 = rs.effective(SYM_PRINT);
            check(p1 && !hasKey(p1, "ctrl+p"), "Reassign steals the colliding Ctrl+P from Print");
            check(p1 && hasKey(p1, "ctrl+r"),  "...but Print KEEPS its unrelated Ctrl+R (keyed unbind, not a bare wipe)");
            const EffectiveBinding* s1 = rs.effective(SYM_SAVE);
            check(s1 && s1->accels.size() == 1 && hasKey(s1, "ctrl+p"),
                  "Reassign leaves Save bound to exactly the stolen Ctrl+P (unbind-then-bind replace)");
            check(s1 && s1->source == BindingSource::User, "the reassigned binding reports source User");

            // the whole edit persists as one consistent user layer
            check(rs.save(), "reassignRebind persists via save()");
            KeymapStore rr;
            rr.addDefault(SYM_SAVE,  kCmdFileSave,  "Ctrl+S");
            rr.addDefault(SYM_PRINT, kCmdFilePrint, "Ctrl+P");
            rr.load(dir);
            const EffectiveBinding* p2 = rr.effective(SYM_PRINT);
            const EffectiveBinding* s2 = rr.effective(SYM_SAVE);
            check(p2 && !hasKey(p2, "ctrl+p") && hasKey(p2, "ctrl+r") && s2 && hasKey(s2, "ctrl+p"),
                  "the steal round-trips through shortcuts.json intact");
        }

        // ---- editor-tier HARD conflict keeps BOTH bindings (no steal on the editor tier) ------------
        // The mapper's editor path offers keep-both-or-cancel only (confirmKeepBoth): assigning anyway
        // must leave the OTHER editor command's key untouched, and both rows must then flag the HARD
        // conflict (the red rows + info line are what inform the user).
        {
            KeymapStore ks;
            seedEditorKeymapDefaults(ks);
            ks.resolveAll();
            ConflictEngine keng;
            keng.rebuild(ks, [](int){ return wxString(); });
            const NormKey ctrlLeft = conflictKeys::fromAccelString("Ctrl+Left");   // editor.wordLeft's key
            check(keng.forProposedEditor("editor.lineCut", SCI_LINECUT, ctrlLeft).cls == ConflictClass::Hard,
                  "editor-vs-editor collision classifies HARD (gates the keep-both confirm)");

            ks.setEditorBinding("editor.lineCut", "Ctrl+Left");     // "Keep both" chosen: assign anyway
            const EditorEffective* moved = ks.editorEffective("editor.lineCut");
            const EditorEffective* kept  = ks.editorEffective("editor.wordLeft");
            check(moved && moved->overridden && keySpell::canonical(moved->effectiveRaw) == "ctrl+left",
                  "keep-both: the moved editor command lands on Ctrl+Left");
            check(kept && !kept->overridden && keySpell::canonical(kept->effectiveRaw) == "ctrl+left",
                  "keep-both: editor.wordLeft KEEPS Ctrl+Left untouched (nothing was stolen)");

            keng.rebuild(ks, [](int){ return wxString(); });
            check(keng.forEditor("editor.lineCut").cls == ConflictClass::Hard,
                  "after keep-both, the moved row flags the HARD conflict");
            check(keng.forEditor("editor.wordLeft").cls == ConflictClass::Hard,
                  "after keep-both, the other editor row flags it too");
        }

        // ---- a read-only (newer-version) store refuses save() ---------------------------------------
        // shortcuts.json with version > kCurrentVersion marks the store read-only; save() must refuse so
        // the newer file's unknown fields are never clobbered. The mapper freezes its mutating UI off
        // isReadOnly() for the same state (edits must not apply live and then silently fail to persist).
        {
            const wxString dir = makeTempSubdir("wxnote_km_readonly");
            const std::string newer =
                "{ \"version\": 2, \"userKeybindings\": [ { \"command\": \"file.save\", \"key\": \"ctrl+alt+s\" } ] }";
            writeTextFile(dir + wxFILE_SEP_PATH + "shortcuts.json", newer);
            KeymapStore ro;
            ro.addDefault(SYM_SAVE, kCmdFileSave, "Ctrl+S");
            ro.load(dir);
            check(ro.isReadOnly(), "a version-2 shortcuts.json marks the store read-only");
            ro.rebind(SYM_SAVE, "Ctrl+B");            // an in-memory edit (the mapper blocks these)...
            check(!ro.save(), "...and save() refuses to write over the newer file");
            check(std::string(readTextFile(dir + wxFILE_SEP_PATH + "shortcuts.json").utf8_str()) == newer,
                  "the newer file's content is byte-identical after the refused save");
        }

        // ---- menu conflict labels resolve LAZILY through the rebuild callback -----------------------
        // rebuild() no longer FindItem-resolves every owner's label eagerly; the label is fetched
        // (memoized) only when a conflict actually names the command, with the symbolicName as the
        // fallback when the resolver has nothing.
        {
            KeymapStore ls;
            ls.addDefault(SYM_SAVE,  kCmdFileSave,  "Ctrl+S");
            ls.addDefault(SYM_PRINT, kCmdFilePrint, "Ctrl+P");
            ls.resolveAll();
            const NormKey ctrlS = conflictKeys::fromAccelString("Ctrl+S");

            ConflictEngine named;
            named.rebuild(ls, [](int id){ return id == kCmdFileSave ? wxString("Save Label") : wxString(); });
            check(named.forProposed(kCmdFilePrint, ctrlS, KeyScope::Global).otherName == "Save Label",
                  "a HARD conflict names the other command via the lazily-resolved menu label");

            ConflictEngine bare;
            bare.rebuild(ls, [](int){ return wxString(); });
            check(bare.forProposed(kCmdFilePrint, ctrlS, KeyScope::Global).otherName == SYM_SAVE,
                  "an empty resolver falls back to the owning command's symbolic name");
        }

        std::printf(g_fail ? "\nFAILED  (%d passed, %d failed)\n" : "\nPASSED  (%d passed, %d failed)\n",
                    g_pass, g_fail);
        std::fflush(stdout);
    }
};

wxIMPLEMENT_APP_CONSOLE(SelfTestApp);
