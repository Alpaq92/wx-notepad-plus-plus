#pragma once
// =====================================================================
// scintilla_keymap_defaults.h - a STATIC DATA MIRROR of Scintilla's built-in editor keymap.
// Scintilla 5.0.0 exposes no runtime dump of its default
// key->command table, so the conflict engine cannot ASK the editor "what does Ctrl+Shift+T do by
// default?". Every editor built on Scintilla that needs this (Notepad++, NotepadNext, ...) hard-copies
// the table; we do the same. This is a faithful copy of MapDefault[] from
//   build/_deps/wxwidgets-src/src/stc/scintilla/src/KeyMap.cxx
// pinned to wx 3.3.1 / Scintilla 5.0.0 (the wx-bundled-scintilla-version-ceiling memory - re-verify this
// file against KeyMap.cxx on any wx bump, or the benign-shadow suppression below silently drifts).
//
// Only the portable (non-OS_X_KEYS) branch is mirrored: it is what actually compiles into Scintilla on
// Windows and Linux, wxNote's default borderless targets. The macOS Ctrl->Cmd remap in KeyMap.cxx's
// OS_X block is a minor advisory divergence and is documented, not mirrored, here.
//
// The conflict engine (conflict_engine.h) unions these editor defaults with the menu bindings and asks:
// does a menu accelerator sit on a key that a Scintilla default also uses, and if so is it the SAME
// action (benign shadow - e.g. Ctrl+C menu Copy over SCI_COPY) or a DIFFERENT one (a real shadow the
// user should be warned about - e.g. Ctrl+Shift+T File>Restore over SCI_LINECOPY)?
// =====================================================================
#include "Scintilla.h"     // SCK_*, SCMOD_*, SCI_* - the same header main.cpp already includes (line ~137)
#include <cstddef>

// One row of Scintilla's default key table: a Scintilla key code (SCK_* or a raw ASCII char) + a
// Scintilla modifier mask (SCMOD_* combination) -> the SCI_* command it fires. `name` is the SCI_*
// symbol as a bare ascii identifier (NOT a translated string - it is a code-level command name, shown
// verbatim inside a translated "%s shadows the editor command %s" frame so the shadow warning names the
// exact command without adding ~65 catalog entries).
struct ScintKeyDefault
{
    int         key;     // SCK_* or an ASCII character code ('Z', '[', ...)
    int         mods;    // SCMOD_* bitmask (0 == no modifier)
    int         cmd;     // SCI_* command id
    const char* name;    // "SCI_LINECOPY" etc. - diagnostic, untranslated
};

// KeyMap.cxx shorthands, reproduced locally (KeyMap.h is an internal Scintilla header, not on the public
// include path). Non-OS_X definitions: SCI_CTRL_META == Ctrl, SCI_SCTRL_META == Ctrl+Shift.
namespace scintKeymap
{
    inline constexpr int Norm   = SCMOD_NORM;                    // 0
    inline constexpr int Shift  = SCMOD_SHIFT;                   // 1
    inline constexpr int Ctrl   = SCMOD_CTRL;                    // 2
    inline constexpr int Alt    = SCMOD_ALT;                     // 4
    inline constexpr int CShift = SCMOD_CTRL | SCMOD_SHIFT;      // 3
    inline constexpr int AShift = SCMOD_ALT  | SCMOD_SHIFT;      // 5

    // The mirror. Order and values follow KeyMap.cxx's MapDefault[] exactly (portable branch), so a diff
    // against the vendored source stays a line-for-line check.
    inline constexpr ScintKeyDefault kMapDefault[] = {
        { SCK_DOWN,   Norm,   SCI_LINEDOWN,            "SCI_LINEDOWN" },
        { SCK_DOWN,   Shift,  SCI_LINEDOWNEXTEND,      "SCI_LINEDOWNEXTEND" },
        { SCK_DOWN,   Ctrl,   SCI_LINESCROLLDOWN,      "SCI_LINESCROLLDOWN" },
        { SCK_DOWN,   AShift, SCI_LINEDOWNRECTEXTEND,  "SCI_LINEDOWNRECTEXTEND" },
        { SCK_UP,     Norm,   SCI_LINEUP,              "SCI_LINEUP" },
        { SCK_UP,     Shift,  SCI_LINEUPEXTEND,        "SCI_LINEUPEXTEND" },
        { SCK_UP,     Ctrl,   SCI_LINESCROLLUP,        "SCI_LINESCROLLUP" },
        { SCK_UP,     AShift, SCI_LINEUPRECTEXTEND,    "SCI_LINEUPRECTEXTEND" },
        { '[',        Ctrl,   SCI_PARAUP,              "SCI_PARAUP" },
        { '[',        CShift, SCI_PARAUPEXTEND,        "SCI_PARAUPEXTEND" },
        { ']',        Ctrl,   SCI_PARADOWN,            "SCI_PARADOWN" },
        { ']',        CShift, SCI_PARADOWNEXTEND,      "SCI_PARADOWNEXTEND" },
        { SCK_LEFT,   Norm,   SCI_CHARLEFT,            "SCI_CHARLEFT" },
        { SCK_LEFT,   Shift,  SCI_CHARLEFTEXTEND,      "SCI_CHARLEFTEXTEND" },
        { SCK_LEFT,   Ctrl,   SCI_WORDLEFT,            "SCI_WORDLEFT" },
        { SCK_LEFT,   CShift, SCI_WORDLEFTEXTEND,      "SCI_WORDLEFTEXTEND" },
        { SCK_LEFT,   AShift, SCI_CHARLEFTRECTEXTEND,  "SCI_CHARLEFTRECTEXTEND" },
        { SCK_RIGHT,  Norm,   SCI_CHARRIGHT,           "SCI_CHARRIGHT" },
        { SCK_RIGHT,  Shift,  SCI_CHARRIGHTEXTEND,     "SCI_CHARRIGHTEXTEND" },
        { SCK_RIGHT,  Ctrl,   SCI_WORDRIGHT,           "SCI_WORDRIGHT" },
        { SCK_RIGHT,  CShift, SCI_WORDRIGHTEXTEND,     "SCI_WORDRIGHTEXTEND" },
        { SCK_RIGHT,  AShift, SCI_CHARRIGHTRECTEXTEND, "SCI_CHARRIGHTRECTEXTEND" },
        { '/',        Ctrl,   SCI_WORDPARTLEFT,        "SCI_WORDPARTLEFT" },
        { '/',        CShift, SCI_WORDPARTLEFTEXTEND,  "SCI_WORDPARTLEFTEXTEND" },
        { '\\',       Ctrl,   SCI_WORDPARTRIGHT,       "SCI_WORDPARTRIGHT" },
        { '\\',       CShift, SCI_WORDPARTRIGHTEXTEND, "SCI_WORDPARTRIGHTEXTEND" },
        { SCK_HOME,   Norm,   SCI_VCHOME,              "SCI_VCHOME" },
        { SCK_HOME,   Shift,  SCI_VCHOMEEXTEND,        "SCI_VCHOMEEXTEND" },
        { SCK_HOME,   Ctrl,   SCI_DOCUMENTSTART,       "SCI_DOCUMENTSTART" },
        { SCK_HOME,   CShift, SCI_DOCUMENTSTARTEXTEND, "SCI_DOCUMENTSTARTEXTEND" },
        { SCK_HOME,   Alt,    SCI_HOMEDISPLAY,         "SCI_HOMEDISPLAY" },
        { SCK_HOME,   AShift, SCI_VCHOMERECTEXTEND,    "SCI_VCHOMERECTEXTEND" },
        { SCK_END,    Norm,   SCI_LINEEND,             "SCI_LINEEND" },
        { SCK_END,    Shift,  SCI_LINEENDEXTEND,       "SCI_LINEENDEXTEND" },
        { SCK_END,    Ctrl,   SCI_DOCUMENTEND,         "SCI_DOCUMENTEND" },
        { SCK_END,    CShift, SCI_DOCUMENTENDEXTEND,   "SCI_DOCUMENTENDEXTEND" },
        { SCK_END,    Alt,    SCI_LINEENDDISPLAY,      "SCI_LINEENDDISPLAY" },
        { SCK_END,    AShift, SCI_LINEENDRECTEXTEND,   "SCI_LINEENDRECTEXTEND" },
        { SCK_PRIOR,  Norm,   SCI_PAGEUP,              "SCI_PAGEUP" },
        { SCK_PRIOR,  Shift,  SCI_PAGEUPEXTEND,        "SCI_PAGEUPEXTEND" },
        { SCK_PRIOR,  AShift, SCI_PAGEUPRECTEXTEND,    "SCI_PAGEUPRECTEXTEND" },
        { SCK_NEXT,   Norm,   SCI_PAGEDOWN,            "SCI_PAGEDOWN" },
        { SCK_NEXT,   Shift,  SCI_PAGEDOWNEXTEND,      "SCI_PAGEDOWNEXTEND" },
        { SCK_NEXT,   AShift, SCI_PAGEDOWNRECTEXTEND,  "SCI_PAGEDOWNRECTEXTEND" },
        { SCK_DELETE, Norm,   SCI_CLEAR,               "SCI_CLEAR" },
        { SCK_DELETE, Shift,  SCI_CUT,                 "SCI_CUT" },
        { SCK_DELETE, Ctrl,   SCI_DELWORDRIGHT,        "SCI_DELWORDRIGHT" },
        { SCK_DELETE, CShift, SCI_DELLINERIGHT,        "SCI_DELLINERIGHT" },
        { SCK_INSERT, Norm,   SCI_EDITTOGGLEOVERTYPE,  "SCI_EDITTOGGLEOVERTYPE" },
        { SCK_INSERT, Shift,  SCI_PASTE,               "SCI_PASTE" },
        { SCK_INSERT, Ctrl,   SCI_COPY,                "SCI_COPY" },
        { SCK_ESCAPE, Norm,   SCI_CANCEL,              "SCI_CANCEL" },
        { SCK_BACK,   Norm,   SCI_DELETEBACK,          "SCI_DELETEBACK" },
        { SCK_BACK,   Shift,  SCI_DELETEBACK,          "SCI_DELETEBACK" },
        { SCK_BACK,   Ctrl,   SCI_DELWORDLEFT,         "SCI_DELWORDLEFT" },
        { SCK_BACK,   Alt,    SCI_UNDO,                "SCI_UNDO" },
        { SCK_BACK,   CShift, SCI_DELLINELEFT,         "SCI_DELLINELEFT" },
        { 'Z',        Ctrl,   SCI_UNDO,                "SCI_UNDO" },
        { 'Y',        Ctrl,   SCI_REDO,                "SCI_REDO" },
        { 'X',        Ctrl,   SCI_CUT,                 "SCI_CUT" },
        { 'C',        Ctrl,   SCI_COPY,                "SCI_COPY" },
        { 'V',        Ctrl,   SCI_PASTE,               "SCI_PASTE" },
        { 'A',        Ctrl,   SCI_SELECTALL,           "SCI_SELECTALL" },
        { SCK_TAB,    Norm,   SCI_TAB,                 "SCI_TAB" },
        { SCK_TAB,    Shift,  SCI_BACKTAB,             "SCI_BACKTAB" },
        { SCK_RETURN, Norm,   SCI_NEWLINE,             "SCI_NEWLINE" },
        { SCK_RETURN, Shift,  SCI_NEWLINE,             "SCI_NEWLINE" },
        { SCK_ADD,    Ctrl,   SCI_ZOOMIN,              "SCI_ZOOMIN" },
        { SCK_SUBTRACT, Ctrl, SCI_ZOOMOUT,             "SCI_ZOOMOUT" },
        { SCK_DIVIDE, Ctrl,   SCI_SETZOOM,             "SCI_SETZOOM" },
        { 'L',        Ctrl,   SCI_LINECUT,             "SCI_LINECUT" },
        { 'L',        CShift, SCI_LINEDELETE,          "SCI_LINEDELETE" },
        { 'T',        CShift, SCI_LINECOPY,            "SCI_LINECOPY" },
        { 'T',        Ctrl,   SCI_LINETRANSPOSE,       "SCI_LINETRANSPOSE" },
        { 'D',        Ctrl,   SCI_SELECTIONDUPLICATE,  "SCI_SELECTIONDUPLICATE" },
        { 'U',        Ctrl,   SCI_LOWERCASE,           "SCI_LOWERCASE" },
        { 'U',        CShift, SCI_UPPERCASE,           "SCI_UPPERCASE" },
    };
    inline constexpr size_t kMapDefaultCount = sizeof(kMapDefault) / sizeof(kMapDefault[0]);
}

// ---- the VACATE TABLE: stock keys the app clears at runtime with no curated row to manage them ------
// Rows of kMapDefault whose stock action a DEFAULT menu accelerator displaces NON-equivalently and that
// no curated editor row (shortcut_labels.h) covers. The curated rows already vacate their own stock keys
// whenever their effective binding diverges (computeEditorOps in shortcut_labels.h - that path handles
// Ctrl+/ wordPartLeft, Ctrl+[ / Ctrl+] paragraphUp/Down, Ctrl+Shift+T lineCopy, Ctrl+Shift+L
// lineDelete); this table is the SINGLE source of truth for the remaining, non-curated vacates.
// computeEditorOps appends one unconditional vacate op per row (so the live editor really clears the
// key), and ConflictEngine::rebuild derives its SciDefault-owner exclusion from those same ops - the
// mapper's conflict report therefore cannot disagree with what the editor actually honours, which is
// what keeps the SHIPPED defaults at zero mapper issues ("no conflicts" on a fresh install).
//
// Decisions, one comment per row:
//   * Ctrl+D -> SCI_SELECTIONDUPLICATE: the Ctrl+D fork's modal resolution gave the chord to
//     Edit > Multi-select Next (6-editor consensus), whose frame accelerator fires first anyway - the
//     stock action was already unreachable. It is also redundant: duplicate-line/selection lives on the
//     Ctrl+Shift+D menu command (kCmdEditDupLine). Vacating makes the runtime honest instead of leaving
//     a dead-but-reported shadow.
struct StockVacatedKey
{
    int         key;     // SCK_* or an ASCII character code - same key space as ScintKeyDefault
    int         mods;    // SCMOD_* bitmask
    int         cmd;     // the SCI_* command the stock keymap bound there (restored on op undo)
    const char* name;    // the SCI_* symbol - diagnostic, untranslated
};
namespace scintKeymap
{
    inline constexpr StockVacatedKey kStockVacated[] = {
        { 'D', Ctrl, SCI_SELECTIONDUPLICATE, "SCI_SELECTIONDUPLICATE" },
    };
    inline constexpr size_t kStockVacatedCount = sizeof(kStockVacated) / sizeof(kStockVacated[0]);
}

// The equivalence set ("BENIGN SHADOW"): menu command id -> the SCI_* command it dispatches.
// A menu accelerator that sits on a Scintilla default key is a BENIGN shadow (no warning) exactly when
// the menu command runs the SAME editor action - e.g. Edit>Copy (Ctrl+C) over SCI_COPY. These are the
// ~10 headline clipboard/line/case ops we enumerate (Ctrl+Z/Y/X/C/V/A, Del, Ctrl+U, Ctrl+Shift+U -
// plus the duplicate-line row, inert at today's defaults since its menu accel moved to Ctrl+Shift+D;
// see its row note). Any OTHER menu accelerator over a Scintilla default is a non-equivalent shadow
// and IS warned about (the one day-one casualty being Ctrl+Shift+T over SCI_LINECOPY).
//
// The ids are the frozen kCmd*/IDM_* values from command_ids.h, hardcoded here as raw integers so this
// header stays free of the command-id include (it is pulled into the conflict engine and the self-test
// alike); each is annotated with its kCmd name for grep-ability.
struct MenuSciEquivalence { int menuCmdId; int sciCmd; };
inline constexpr MenuSciEquivalence kMenuSciEquivalence[] = {
    { 42003, SCI_UNDO },                 // kCmdEditUndo         <-> SCI_UNDO   (Ctrl+Z)
    { 42004, SCI_REDO },                 // kCmdEditRedo         <-> SCI_REDO   (Ctrl+Y)
    { 42001, SCI_CUT },                  // kCmdEditCut          <-> SCI_CUT    (Ctrl+X)
    { 42002, SCI_COPY },                 // kCmdEditCopy         <-> SCI_COPY   (Ctrl+C)
    { 42005, SCI_PASTE },                // kCmdEditPaste        <-> SCI_PASTE  (Ctrl+V)
    { 42007, SCI_SELECTALL },            // kCmdEditSelectall    <-> SCI_SELECTALL (Ctrl+A)
    { 42006, SCI_CLEAR },                // kCmdEditDelete       <-> SCI_CLEAR  (Del)
    { 42010, SCI_SELECTIONDUPLICATE },   // kCmdEditDupLine      <-> SCI_SELECTIONDUPLICATE. Menu default
                                         // is now Ctrl+Shift+D (the Ctrl+D fork went to multiselect-next),
                                         // so this row is inert at the defaults - kept because the
                                         // equivalence itself still holds: a user who rebinds dup-line
                                         // onto Ctrl+D lands on Scintilla's stock SCI_SELECTIONDUPLICATE
                                         // key, and the multiselect owner still raises HARD first.
    { 42017, SCI_LOWERCASE },            // kCmdEditLowercase    <-> SCI_LOWERCASE (Ctrl+U)
    { 42016, SCI_UPPERCASE },            // kCmdEditUppercase    <-> SCI_UPPERCASE (Ctrl+Shift+U)
    { 42011, SCI_LINETRANSPOSE },        // kCmdEditTransposeLine<-> SCI_LINETRANSPOSE (no default menu accel; kept for completeness)
};
inline constexpr size_t kMenuSciEquivalenceCount = sizeof(kMenuSciEquivalence) / sizeof(kMenuSciEquivalence[0]);

// The SCI_* command a menu command is equivalent to, or 0 if it has no editor-command equivalent.
inline int sciEquivalentForMenuCmd(int menuCmdId)
{
    for (const MenuSciEquivalence& e : kMenuSciEquivalence)
        if (e.menuCmdId == menuCmdId) return e.sciCmd;
    return 0;
}
