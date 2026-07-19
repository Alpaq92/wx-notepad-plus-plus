#pragma once
// =====================================================================
// shortcut_labels.h - the curated "Editor commands" table for the Scintilla editor tier.
// Scintilla owns its own per-instance key->command
// keymap (arrows, word/paragraph navigation, line operations, ...), entirely separate from the menu
// accelerators the rest of the shortcut system manages. This header exposes a hand-picked subset of those
// SCI_* editor commands as remappable rows so a user can rebind them in the Shortcut Mapper, and the
// KeymapStore persists the overrides in shortcuts.json's "editor:" section keyed by the stable ascii
// `name` here ("editor.lineCut" -> SCI_LINECUT).
//
// The set is deliberately CURATED, not the whole ~65-row Scintilla default table:
//   * commands that already have a menu row (Copy/Cut/Paste/Undo/Redo/SelectAll/Uppercase/... - see
//     kMenuSciEquivalence in scintilla_keymap_defaults.h) are EXCLUDED, so a command is remappable in
//     exactly one place instead of listed twice;
//   * the bare unmodified caret keys (Home/End/arrows with no modifier) are EXCLUDED so a stray rebind
//     can't strand a user without basic editing keys.
// What remains is the genuinely-useful-to-remap editor operations: word/word-part/paragraph navigation
// and its selection-extend variants, document start/end, line scrolling, display-line home/end, the line
// cut/copy/delete operations, word/line deletion, and overtype toggle.
//
// Each row carries the STOCK Scintilla key as a wx (keycode, accel-flags) pair copied from Scintilla's
// MapDefault (the same source scintilla_keymap_defaults.h mirrors) rather than as a hand-typed accel
// string, plus - where wxNote's curated default diverges from stock (the 6-editor consensus adoption:
// lineDelete on Ctrl+Shift+K; lineCopy/paragraphUp/paragraphDown/wordPartLeft unbound because a menu
// accelerator owns their stock chord) - an explicit default (key, flags) pair. The store's default accel
// string is DERIVED via wxAcceleratorEntry::ToRawString(), which guarantees the stored spelling always
// reparses (a hand-typed "Ctrl+[" that some wx build couldn't reparse would silently drop the binding).
// main.cpp's computeEditorOps applies any default-vs-stock divergence to the live STCs exactly like a
// user override (vacate stock, assign default). Display names are wxTRANSLATE-marked so xgettext extracts
// them and shown via wxGetTranslation() so they follow the UI language (these MUST be translated).
// =====================================================================
#include "keymap_store.h"      // KeymapStore::addEditorDefault (seedEditorKeymapDefaults feeds Tier 0 editor)
#include "Scintilla.h"         // SCI_*, SCK_*, SCMOD_* - same header main.cpp / scintilla_keymap_defaults.h use
#include "scintilla_keymap_defaults.h"   // kMapDefault mirror + the kStockVacated vacate table (computeEditorOps)
#include <wx/defs.h>           // WXK_*
#include <wx/accel.h>          // wxAcceleratorEntry, wxACCEL_*
#include <wx/translation.h>    // wxGetTranslation, wxTRANSLATE
#include <wx/string.h>
#include <cstddef>
#include <vector>

// One remappable editor command. `wxKey`/`wxMods` are the wx-space mirror of the command's STOCK
// Scintilla default (WXK_*/ascii char + wxACCEL_* flags) - the key Scintilla's own MapDefault binds,
// which main.cpp's computeEditorOps vacates whenever the effective binding diverges; `sciCmd` is the
// SCI_* it fires; `name` is the stable shortcuts.json key; `displayName` is the wxTRANSLATE-marked
// English label shown (translated) in the mapper.
//
// `defWxKey`/`defWxMods` are wxNote's CURATED DEFAULT for the row, which - since the 6-editor consensus
// adoption - may DIVERGE from the stock key:
//   defWxKey == -1  (the NSDMI default)  ->  the curated default IS the stock key (most rows);
//   defWxKey ==  0                       ->  no default binding (unbound-by-default, still remappable);
//   anything else                        ->  a divergent default key (e.g. lineDelete on Ctrl+Shift+K).
// The stock pair stays authoritative for what the un-remapped LIVE Scintilla keymap does (the conflict
// engine's static mirror + computeEditorOps' vacate step); the default pair is what the store seeds and
// the mapper shows/restores.
struct EditorCommandDef
{
    const char* name;         // "editor.lineCut" - stable ascii, persisted; never shown to users
    int         sciCmd;       // SCI_LINECUT
    int         wxKey;        // STOCK key: WXK_* named key, or an uppercase ASCII char ('L', '[', ...)
    int         wxMods;       // STOCK wxACCEL_* bitmask (wxACCEL_NORMAL == none)
    const char* displayName;  // wxTRANSLATE("...") - translated at display via wxGetTranslation
    int         defWxKey  = -1;  // curated default key: -1 = same as stock, 0 = unbound, else divergent
    int         defWxMods = 0;   // curated default mods (meaningful only when defWxKey > 0)
};

// The curated-default (key, mods) pair for a row, resolving the -1 "same as stock" sentinel.
inline int editorDefaultWxKey(const EditorCommandDef& d)  { return d.defWxKey == -1 ? d.wxKey  : d.defWxKey; }
inline int editorDefaultWxMods(const EditorCommandDef& d) { return d.defWxKey == -1 ? d.wxMods : d.defWxMods; }

// The curated set (a focused, high-value subset kept moderate so each name gets a
// real translation in all catalogs). Order here is the mapper's row order for the Editor scope. STOCK
// keys (fields 3-4) mirror scintilla_keymap_defaults.h's portable (non-OS_X) branch exactly; rows with
// trailing defWxKey/defWxMods fields carry a curated default that diverges from stock (see the header
// comment - consensus adoption).
inline const EditorCommandDef kEditorCommands[] = {
    { "editor.wordLeft",            SCI_WORDLEFT,            WXK_LEFT,   wxACCEL_CTRL,               wxTRANSLATE("Move Word Left") },
    { "editor.wordRight",           SCI_WORDRIGHT,           WXK_RIGHT,  wxACCEL_CTRL,               wxTRANSLATE("Move Word Right") },
    { "editor.wordLeftExtend",      SCI_WORDLEFTEXTEND,      WXK_LEFT,   wxACCEL_CTRL | wxACCEL_SHIFT, wxTRANSLATE("Extend Selection Word Left") },
    { "editor.wordRightExtend",     SCI_WORDRIGHTEXTEND,     WXK_RIGHT,  wxACCEL_CTRL | wxACCEL_SHIFT, wxTRANSLATE("Extend Selection Word Right") },
    // Ctrl+/ now belongs to Edit > Comment/Uncomment > Toggle Single Line Comment (the 6/6-unanimous
    // consensus chord, menu_data_edit.h) - the menu accelerator fires first, so the stock Ctrl+/ default
    // here would be dead anyway; unbound-by-default, remappable.
    { "editor.wordPartLeft",        SCI_WORDPARTLEFT,        '/',        wxACCEL_CTRL,               wxTRANSLATE("Move to Previous Word Part"), 0, 0 },
    { "editor.wordPartRight",       SCI_WORDPARTRIGHT,       '\\',       wxACCEL_CTRL,               wxTRANSLATE("Move to Next Word Part") },
    // Ctrl+[ / Ctrl+] now belong to Edit > Indent (the strong-4 consensus indent/outdent chords,
    // menu_data_edit.h) - Scintilla's paragraph-up/down stock defaults are cleared here (the extend
    // variants Ctrl+Shift+[ / Ctrl+Shift+] stay stock); both commands stay remappable.
    { "editor.paragraphUp",         SCI_PARAUP,              '[',        wxACCEL_CTRL,               wxTRANSLATE("Move to Previous Paragraph"), 0, 0 },
    { "editor.paragraphDown",       SCI_PARADOWN,            ']',        wxACCEL_CTRL,               wxTRANSLATE("Move to Next Paragraph"), 0, 0 },
    { "editor.documentStart",       SCI_DOCUMENTSTART,       WXK_HOME,   wxACCEL_CTRL,               wxTRANSLATE("Go to Start of Document") },
    { "editor.documentEnd",         SCI_DOCUMENTEND,         WXK_END,    wxACCEL_CTRL,               wxTRANSLATE("Go to End of Document") },
    { "editor.documentStartExtend", SCI_DOCUMENTSTARTEXTEND, WXK_HOME,   wxACCEL_CTRL | wxACCEL_SHIFT, wxTRANSLATE("Extend Selection to Start of Document") },
    { "editor.documentEndExtend",   SCI_DOCUMENTENDEXTEND,   WXK_END,    wxACCEL_CTRL | wxACCEL_SHIFT, wxTRANSLATE("Extend Selection to End of Document") },
    { "editor.scrollLineUp",        SCI_LINESCROLLUP,        WXK_UP,     wxACCEL_CTRL,               wxTRANSLATE("Scroll Up One Line") },
    { "editor.scrollLineDown",      SCI_LINESCROLLDOWN,      WXK_DOWN,   wxACCEL_CTRL,               wxTRANSLATE("Scroll Down One Line") },
    { "editor.displayLineStart",    SCI_HOMEDISPLAY,         WXK_HOME,   wxACCEL_ALT,                wxTRANSLATE("Go to Start of Display Line") },
    { "editor.displayLineEnd",      SCI_LINEENDDISPLAY,      WXK_END,    wxACCEL_ALT,                wxTRANSLATE("Go to End of Display Line") },
    { "editor.lineCut",             SCI_LINECUT,             'L',        wxACCEL_CTRL,               wxTRANSLATE("Cut Current Line") },
    // Delete-line adopts the strong-4 consensus Ctrl+Shift+K (VSCode/TextMate/Sublime/Pulsar); the stock
    // Ctrl+Shift+L is vacated by computeEditorOps so the shown default is also the live one.
    { "editor.lineDelete",          SCI_LINEDELETE,          'L',        wxACCEL_CTRL | wxACCEL_SHIFT, wxTRANSLATE("Delete Current Line"), 'K', wxACCEL_CTRL | wxACCEL_SHIFT },
    // Ctrl+Shift+T belongs to File > Restore Recently Closed File (the 3/3 reopen-closed-tab consensus,
    // and wxNote's long-standing binding) - the menu accelerator shadowed this stock default into a dead
    // key. Unbound-by-default resolves that day-one shadow; the command stays remappable, and modern
    // editors treat copy-line as empty-selection Ctrl+C behavior rather than a dedicated chord anyway.
    { "editor.lineCopy",            SCI_LINECOPY,            'T',        wxACCEL_CTRL | wxACCEL_SHIFT, wxTRANSLATE("Copy Current Line"), 0, 0 },
    { "editor.deleteWordLeft",      SCI_DELWORDLEFT,         WXK_BACK,   wxACCEL_CTRL,               wxTRANSLATE("Delete Word Left") },
    { "editor.deleteWordRight",     SCI_DELWORDRIGHT,        WXK_DELETE, wxACCEL_CTRL,               wxTRANSLATE("Delete Word Right") },
    { "editor.deleteLineLeft",      SCI_DELLINELEFT,         WXK_BACK,   wxACCEL_CTRL | wxACCEL_SHIFT, wxTRANSLATE("Delete to Start of Line") },
    { "editor.deleteLineRight",     SCI_DELLINERIGHT,        WXK_DELETE, wxACCEL_CTRL | wxACCEL_SHIFT, wxTRANSLATE("Delete to End of Line") },
    { "editor.toggleOvertype",      SCI_EDITTOGGLEOVERTYPE,  WXK_INSERT, wxACCEL_NORMAL,             wxTRANSLATE("Toggle Overtype Mode") },
};
inline constexpr size_t kEditorCommandCount = sizeof(kEditorCommands) / sizeof(kEditorCommands[0]);

// The default accel string (canonical, ToRawString) for a curated row - the CURATED default pair
// (editorDefaultWxKey/Mods), not necessarily the stock one. Built via a real wxAcceleratorEntry, so the
// string is guaranteed to reparse with FromString() (unlike a hand-typed spelling). Locale-independent -
// ToRawString uses invariant English tokens. Empty for an unbound-by-default row (defWxKey == 0).
inline wxString editorDefaultAccelRaw(const EditorCommandDef& d)
{
    const int key = editorDefaultWxKey(d);
    if (key == 0) return wxString();
    return wxAcceleratorEntry(editorDefaultWxMods(d), key, 0).ToRawString();
}

// Translated display name for a stable editor-command name, or the name verbatim if unknown (defensive -
// a name from a newer shortcuts.json that this build doesn't define).
inline wxString editorCommandDisplayName(const wxString& name)
{
    for (size_t i = 0; i < kEditorCommandCount; ++i)
        if (name == kEditorCommands[i].name)
            return wxGetTranslation(wxString::FromAscii(kEditorCommands[i].displayName));
    return name;
}

// The curated row for a stable name, or nullptr.
inline const EditorCommandDef* editorCommandByName(const wxString& name)
{
    for (size_t i = 0; i < kEditorCommandCount; ++i)
        if (name == kEditorCommands[i].name) return &kEditorCommands[i];
    return nullptr;
}

// ---- wx <-> Scintilla key/modifier translation for CmdKeyAssign/CmdKeyClear --------------
// wxSTC's CmdKeyAssign(key, modifiers, cmd) speaks Scintilla's key space (SCK_*/SCMOD_*), which differs
// from wx's (WXK_*/wxACCEL_*) for the NAMED keys (arrows, Home/End, Delete, ...) and for ESCAPE (SCK 7 vs
// WXK 27). Letters/punctuation share the numeric value; letters must be UPPERCASE for Scintilla's keymap
// (its MapDefault uses 'Z','C','L',...). This is the inverse of conflict_engine.h's wxKeyFromScintilla.
inline int sciKeyFromWx(int wxKey)
{
    switch (wxKey)
    {
        case WXK_DOWN:             return SCK_DOWN;
        case WXK_UP:               return SCK_UP;
        case WXK_LEFT:             return SCK_LEFT;
        case WXK_RIGHT:            return SCK_RIGHT;
        case WXK_HOME:             return SCK_HOME;
        case WXK_END:              return SCK_END;
        case WXK_PAGEUP:           return SCK_PRIOR;
        case WXK_PAGEDOWN:         return SCK_NEXT;
        case WXK_DELETE:           return SCK_DELETE;
        case WXK_INSERT:           return SCK_INSERT;
        case WXK_ESCAPE:           return SCK_ESCAPE;         // 27 -> 7
        case WXK_BACK:             return SCK_BACK;           // 8  == SCK_BACK
        case WXK_TAB:              return SCK_TAB;            // 9  == SCK_TAB
        case WXK_RETURN:           return SCK_RETURN;         // 13 == SCK_RETURN
        case WXK_NUMPAD_ADD:       return SCK_ADD;
        case WXK_NUMPAD_SUBTRACT:  return SCK_SUBTRACT;
        case WXK_NUMPAD_DIVIDE:    return SCK_DIVIDE;
        default:                   return (wxKey >= 'a' && wxKey <= 'z') ? (wxKey - 'a' + 'A') : wxKey;
    }
}
inline int sciModsFromWx(int accelFlags)
{
    int m = SCMOD_NORM;
    // wxACCEL_CMD is an alias of wxACCEL_CTRL, and wxACCEL_RAW_CTRL == wxACCEL_CTRL off macOS; fold all to
    // Scintilla's Ctrl (the editor keymap is Ctrl-based; the macOS Cmd nuance is advisory).
    if (accelFlags & (wxACCEL_CTRL | wxACCEL_CMD | wxACCEL_RAW_CTRL)) m |= SCMOD_CTRL;
    if (accelFlags & wxACCEL_ALT)   m |= SCMOD_ALT;
    if (accelFlags & wxACCEL_SHIFT) m |= SCMOD_SHIFT;
    return m;
}

// Seed the KeymapStore's editor Tier 0 (the curated defaults) - call once at startup alongside
// seedKeymapDefaults(), before load(). Each row's default accel is the ToRawString() form so it is a
// valid, reparseable canonical spelling.
inline void seedEditorKeymapDefaults(KeymapStore& store)
{
    for (size_t i = 0; i < kEditorCommandCount; ++i)
        store.addEditorDefault(wxString::FromAscii(kEditorCommands[i].name),
                               kEditorCommands[i].sciCmd,
                               editorDefaultAccelRaw(kEditorCommands[i]));
}

// ---- the editor-keymap OP SET (shared by main.cpp, conflict_engine.h and keymap_selftest) -----------
// The single computation behind "which stock Scintilla keys does the app vacate, and what replaces
// them". One op per curated row whose EFFECTIVE binding diverges from its stock key (vacate stock,
// assign the effective key when there is one), plus one unconditional vacate per
// scintKeymap::kStockVacated row (stock defaults a DEFAULT menu chord displaces non-equivalently with
// no curated row to manage them - see the table's rationale). Shared on purpose:
//   * main.cpp's reapplyEditorKeymaps/applyEditorOpsTo applies the ops to the live STCs
//     (CmdKeyClear/CmdKeyAssign);
//   * ConflictEngine::rebuild derives its SciDefault-owner exclusion from the SAME ops, so the mapper's
//     conflict report can never disagree with what the editor actually honours;
//   * keymap_selftest's "defaults are conflict-free" invariant asserts the two stay lock-stepped.
struct EditorOp
{
    int  sciCmd = 0;
    int  defKey = 0, defMods = 0;   // the command's stock default key (Scintilla space) - vacated
    bool hasOverride = false;
    int  ovKey = 0, ovMods = 0;     // the override key (Scintilla space) - assigned when hasOverride
};

// The stock SCI command bound to a (Scintilla) key by Scintilla's default keymap, or 0 if none - used
// to RESTORE a key an override had stolen back to its built-in behaviour on undo.
inline int stockSciCommandFor(int sciKey, int sciMods)
{
    for (size_t i = 0; i < scintKeymap::kMapDefaultCount; ++i)
    {
        const ScintKeyDefault& d = scintKeymap::kMapDefault[i];
        if (d.key == sciKey && d.mods == sciMods) return d.cmd;
    }
    return 0;
}

// The set of editor rows whose effective binding DIVERGES from the stock Scintilla key, translated
// into Scintilla key space - user/scheme overrides AND the curated defaults that differ from stock
// (the consensus adoption); a row sitting at its stock key is omitted so its stock binding is never
// disturbed. The kStockVacated pure vacates are appended unconditionally at the end.
inline std::vector<EditorOp> computeEditorOps(const KeymapStore& store)
{
    std::vector<EditorOp> ops;
    for (const EditorEffective& e : store.editorAll())
    {
        const EditorCommandDef* d = editorCommandByName(e.name);
        if (!d) continue;
        EditorOp op;
        op.sciCmd  = e.sciCmd;
        op.defKey  = sciKeyFromWx(d->wxKey);
        op.defMods = sciModsFromWx(d->wxMods);
        if (!e.effectiveRaw.empty())
        {
            wxAcceleratorEntry ae;
            if (ae.FromString(e.effectiveRaw))
            {
                op.hasOverride = true;
                op.ovKey  = sciKeyFromWx(ae.GetKeyCode());
                op.ovMods = sciModsFromWx(ae.GetFlags());
            }
            // an unparsable override degrades to a clear (hasOverride false): the default key is
            // vacated and nothing re-bound, i.e. the command becomes unbound - matches an explicit clear.
        }
        // At the stock key (the common case: a curated default that IS the stock key, or a user
        // override placed back onto it) there is nothing to manage - leave the stock binding alone.
        if (op.hasOverride && op.ovKey == op.defKey && op.ovMods == op.defMods) continue;
        ops.push_back(op);
    }
    for (const StockVacatedKey& v : scintKeymap::kStockVacated)
    {
        EditorOp op;                 // a pure vacate: no curated row, nothing re-bound
        op.sciCmd  = v.cmd;
        op.defKey  = v.key;
        op.defMods = v.mods;
        ops.push_back(op);
    }
    return ops;
}
