#pragma once
// =====================================================================
// conflict_engine.h - classifies keyboard-shortcut collisions for the mapper. It unions THREE binding
// sources onto one normalized (modifiers, key) space and, for any
// command or any proposed new binding, answers "does this key already do something, and does that
// matter?":
//
//   1. Menu bindings      - the effective set from KeymapStore (each carries a KeyScope).
//   2. Scintilla defaults - the static MapDefault[] mirror (scintilla_keymap_defaults.h), all Editor
//                           scope, MINUS the rows the app vacates at runtime (computeEditorOps'
//                           vacate set, shortcut_labels.h) - the report only counts what the live
//                           editor actually honours.
//   3. Scoped hardcoded   - the terminal Ctrl+Shift+Up/Down chrome-escape chords (terminal_panel.h:1092),
//      chords                Terminal scope. (Find-dialog Enter/Esc etc. are a later phase.)
//
// Four outcome classes:
//   HARD            - two owners on the same key whose scopes can be simultaneously active and that are
//                     NOT the same action. The real conflict; the mapper warns and prompts.
//   NON_EQUIV_SHADOW- a menu accelerator masks a DIFFERENT Scintilla default (e.g. Ctrl+Shift+T over
//                     SCI_LINECOPY). Warn, don't block.
//   BENIGN_SHADOW   - a menu accelerator over the Scintilla default that runs the SAME action (Ctrl+C
//                     over SCI_COPY). Silent (kMenuSciEquivalence).
//   SCOPED_OK       - same key in scopes the focus gate keeps mutually exclusive (terminal Ctrl+Shift+Up
//                     vs Edit>Move Line Up). Silent - this is the intentional coexistence class.
//
// The NormKey space is wx's: menu accelerators are already wx (flags,keycode); the Scintilla mirror is
// converted SCK_*->WXK_* / SCMOD_*->wxACCEL_* so the two are comparable. Modifiers are folded to a
// Ctrl/Alt/Shift triple (Cmd/RawCtrl/Meta/Super all fold into Ctrl) - enough for an advisory engine and
// correct on wxNote's default Win/Linux targets; the macOS Cmd/Ctrl nuance is documented in the plan.
// =====================================================================
#include "keymap_store.h"
#include "scintilla_keymap_defaults.h"
#include "shortcut_labels.h"     // editorCommandDisplayName - names an editor-command owner in a conflict
#include <wx/accel.h>
#include <wx/defs.h>
#include <wx/string.h>
#include <vector>
#include <unordered_map>
#include <functional>
#include <algorithm>

// ---- normalized key ---------------------------------------------------------------------------------
struct NormKey
{
    int mods = 0;   // bit0 Ctrl, bit1 Alt, bit2 Shift (see kMod* below)
    int key  = 0;   // canonical wx keycode: WXK_* for named keys, uppercase ASCII for letters/punct
    bool operator==(const NormKey& o) const { return mods == o.mods && key == o.key; }
    bool valid() const { return key != 0; }
};
inline constexpr int kModCtrl = 1, kModAlt = 2, kModShift = 4;

struct NormKeyHash
{
    size_t operator()(const NormKey& k) const { return (size_t)k.key * 131u + (size_t)k.mods; }
};

namespace conflictKeys
{
    // Fold wx accelerator flags -> the Ctrl/Alt/Shift triple. wxACCEL_CMD is an alias of wxACCEL_CTRL on
    // every platform, and wxACCEL_RAW_CTRL == wxACCEL_CTRL off macOS; OR them so a mac Cmd binding still
    // compares equal to a Ctrl one (advisory engine - see header note).
    inline int modsFromWx(int flags)
    {
        int m = 0;
        if (flags & (wxACCEL_CTRL | wxACCEL_CMD | wxACCEL_RAW_CTRL)) m |= kModCtrl;
        if (flags & wxACCEL_ALT)   m |= kModAlt;
        if (flags & wxACCEL_SHIFT) m |= kModShift;
        return m;
    }
    inline int modsFromScintilla(int sciMods)
    {
        int m = 0;
        if (sciMods & (SCMOD_CTRL | SCMOD_META | SCMOD_SUPER)) m |= kModCtrl;
        if (sciMods & SCMOD_ALT)   m |= kModAlt;
        if (sciMods & SCMOD_SHIFT) m |= kModShift;
        return m;
    }
    inline int keyUpper(int k) { return (k >= 'a' && k <= 'z') ? (k - 'a' + 'A') : k; }

    // Scintilla key code -> wx keycode, so a Scintilla default and a menu accelerator on the "same" named
    // key land on the same NormKey. Letters/punctuation (and the ASCII-range control keys BACK/TAB/RETURN,
    // which happen to share wx's numeric value) pass through; only the named keys and ESCAPE (SCK 7 vs WXK
    // 27) need remapping.
    inline int wxKeyFromScintilla(int sck)
    {
        switch (sck)
        {
            case SCK_DOWN:     return WXK_DOWN;
            case SCK_UP:       return WXK_UP;
            case SCK_LEFT:     return WXK_LEFT;
            case SCK_RIGHT:    return WXK_RIGHT;
            case SCK_HOME:     return WXK_HOME;
            case SCK_END:      return WXK_END;
            case SCK_PRIOR:    return WXK_PAGEUP;
            case SCK_NEXT:     return WXK_PAGEDOWN;
            case SCK_DELETE:   return WXK_DELETE;
            case SCK_INSERT:   return WXK_INSERT;
            case SCK_ESCAPE:   return WXK_ESCAPE;      // 7 -> 27
            case SCK_BACK:     return WXK_BACK;        // 8  == WXK_BACK
            case SCK_TAB:      return WXK_TAB;         // 9  == WXK_TAB
            case SCK_RETURN:   return WXK_RETURN;      // 13 == WXK_RETURN
            case SCK_ADD:      return WXK_NUMPAD_ADD;
            case SCK_SUBTRACT: return WXK_NUMPAD_SUBTRACT;
            case SCK_DIVIDE:   return WXK_NUMPAD_DIVIDE;
            default:           return keyUpper(sck);   // an ASCII char ('Z', '[', ...)
        }
    }

    inline NormKey fromWx(int flags, int keyCode) { return { modsFromWx(flags), keyUpper(keyCode) }; }
    inline NormKey fromScintilla(int sck, int sciMods) { return { modsFromScintilla(sciMods), wxKeyFromScintilla(sck) }; }

    // Parse a canonical accelerator string ("Ctrl+Shift+T") to a NormKey; invalid/chord -> {0,0}.
    inline NormKey fromAccelString(const wxString& raw)
    {
        if (raw.empty() || keySpell::isChord(raw)) return {};
        wxAcceleratorEntry e;
        if (!e.FromString(raw)) return {};
        return fromWx(e.GetFlags(), e.GetKeyCode());
    }

    // Rebuild a NormKey into a wx accelerator flag set for display via wxAcceleratorEntry::ToString().
    inline int wxFlagsFromMods(int mods)
    {
        int f = wxACCEL_NORMAL;
        if (mods & kModCtrl)  f |= wxACCEL_CTRL;
        if (mods & kModAlt)   f |= wxACCEL_ALT;
        if (mods & kModShift) f |= wxACCEL_SHIFT;
        return f;
    }
    inline wxString display(const NormKey& k)
    {
        if (!k.valid()) return wxString();
        wxAcceleratorEntry e(wxFlagsFromMods(k.mods), k.key, 0);
        const wxString s = e.ToString();
        return s.empty() ? e.ToRawString() : s;
    }
}

// ---- ownership + classification ---------------------------------------------------------------------
// Menu       - a menu command's effective binding (frame accelerator).
// SciDefault - a row of Scintilla's STATIC default keymap mirror (scintilla_keymap_defaults.h).
// Editor     - a curated, remappable editor command's EFFECTIVE binding (its live key, default or user-
//              overridden), from the store's editor tier (plan 5.4). SciDefault and Editor are both
//              "editorish": a menu key over either is a shadow, not a hard conflict.
// Scoped     - a hardcoded scope-limited chord (the terminal chrome-escape keys).
enum class OwnerKind { Menu, SciDefault, Editor, Scoped };

struct KeyOwner
{
    OwnerKind kind = OwnerKind::Menu;
    KeyScope  scope = KeyScope::Global;
    BindingSource src = BindingSource::Default; // where the binding came from (Menu/Editor owners; a
                                                // SciDefault mirror row is by definition Default)
    int       cmdId = 0;        // menu command id (Menu), else 0
    int       sciCmd = 0;       // SCI_* (SciDefault/Editor), or the menu command's SCI equivalent (Menu), else 0
    wxString  name;             // display name: menu label / SCI symbol / editor-command label / a scope hint
    wxString  editorName;       // stable editor-command name (Editor owners only) - identifies the row
};

enum class ConflictClass { None, ScopedOk, BenignShadow, NonEquivShadow, Hard };

struct ConflictInfo
{
    ConflictClass cls = ConflictClass::None;
    NormKey  key;
    wxString otherName;              // the conflicting owner's display name
    wxString otherSciName;           // SCI_* symbol when the other owner is a Scintilla default (else empty)
    int      otherCmdId = 0;         // the conflicting MENU command's id (0 if the other owner isn't a menu cmd)
    bool     otherIsEditorDefault = false;
    KeyScope selfScope  = KeyScope::Global;   // the queried owner's scope...
    KeyScope otherScope = KeyScope::Global;   // ...and the conflicting owner's - so the mapper's info line
                                              // can name the other side's scope when the two differ
    bool isWarning() const { return cls == ConflictClass::Hard || cls == ConflictClass::NonEquivShadow; }
};

// One distinct conflicted key for the mapper's "Details..." report: the bucket's worst pairwise class
// plus EVERY owner sitting on that key (menu names resolved through the lazy resolver). Only real
// issues (Hard / NonEquivShadow) become issues - BenignShadow and ScopedOk coexistence is by design
// and would drown the report in non-problems.
struct ConflictIssue
{
    ConflictClass         cls = ConflictClass::None;
    NormKey               key;
    wxString              keyDisplay;   // localized chord spelling (conflictKeys::display)
    std::vector<KeyOwner> owners;       // every owner in the key's bucket, bucket (insertion) order
};

class ConflictEngine
{
public:
    // (Re)index every source. `menuName(cmdId)` resolves a menu command's display label (the mapper feeds
    // it wxMenuBar::FindItem(id)->GetItemLabelText()). The engine RETAINS the callback and resolves menu
    // labels lazily, memoized, the first time a conflict actually NAMES that command (see menuOwnerName) -
    // eagerly FindItem-resolving every owner here cost a full menu-bar walk per command though only the
    // handful of conflicting commands ever display a name. The callback must outlive the engine or the
    // next rebuild(); the mapper's captures its own members, so their lifetimes already match. Editor /
    // Scintilla owner names stay eagerly copied - they are the only name source for non-menu owners.
    void rebuild(const KeymapStore& store, const std::function<wxString(int)>& menuName)
    {
        m_buckets.clear();
        m_cmdKeys.clear();
        m_editorKeys.clear();
        m_menuName = menuName;
        m_menuNameCache.clear();

        // 1. Menu bindings (effective set). Chords and empty accels are skipped - the accel table and this
        //    engine both only speak plain key+modifier bindings in v1.
        for (const EffectiveBinding* b : store.all())
        {
            if (!b || b->cmdId == 0) continue;
            for (const EffectiveAccel& a : b->accels)
            {
                if (a.isChord || a.raw.empty()) continue;
                const NormKey nk = conflictKeys::fromAccelString(a.raw);
                if (!nk.valid()) continue;
                KeyOwner o;
                o.kind   = OwnerKind::Menu;
                o.scope  = a.scope;
                o.src    = b->source;
                o.cmdId  = b->cmdId;
                o.sciCmd = sciEquivalentForMenuCmd(b->cmdId);
                o.name   = b->symbolicName;   // fallback only; the label resolves lazily (menuOwnerName)
                m_buckets[nk].push_back(std::move(o));
                m_cmdKeys[b->cmdId].push_back(nk);
            }
        }

        // 2. Scintilla editor defaults (all Editor scope). The STATIC mirror - what an un-remapped editor
        //    does. It stays the authoritative source for benign/non-equivalent shadow detection against the
        //    ~65 editor commands, including the ones NOT in the curated remappable set.
        //
        //    MINUS the VACATED rows: the app clears some stock keys at runtime (reapplyEditorKeymaps in
        //    main.cpp - curated rows whose effective binding left their stock key, plus the static
        //    scintKeymap::kStockVacated extras such as Ctrl+D/SCI_SELECTIONDUPLICATE). A vacated stock key
        //    no longer fires its MapDefault command, so counting its mirror row as an owner would report
        //    shadows the live editor cannot produce - and would break the "zero issues on a fresh install"
        //    contract the mapper's report makes. Deriving the exclusion from the SAME computeEditorOps
        //    call main.cpp applies (shortcut_labels.h) keeps the two lock-stepped by construction; the
        //    exclusion happens at insertion, so classify/allIssues/forCommand/forProposed all agree.
        std::unordered_map<NormKey, int, NormKeyHash> vacated;   // stock key -> the SCI_* cmd vacated off it
        for (const EditorOp& op : computeEditorOps(store))
            vacated.emplace(conflictKeys::fromScintilla(op.defKey, op.defMods), op.sciCmd);
        for (size_t i = 0; i < scintKeymap::kMapDefaultCount; ++i)
        {
            const ScintKeyDefault& d = scintKeymap::kMapDefault[i];
            const NormKey nk = conflictKeys::fromScintilla(d.key, d.mods);
            if (!nk.valid()) continue;
            const auto vi = vacated.find(nk);
            if (vi != vacated.end() && vi->second == d.cmd) continue;   // vacated at runtime - not a live owner
            KeyOwner o;
            o.kind   = OwnerKind::SciDefault;
            o.scope  = KeyScope::Editor;
            o.sciCmd = d.cmd;
            o.name   = wxString::FromAscii(d.name);
            m_buckets[nk].push_back(std::move(o));
        }

        // 2b. Curated editor commands at their EFFECTIVE (possibly user-remapped) key (plan 5.4). These are
        //     the mapper's Editor rows as first-class owners, so a key a user moves an editor command onto
        //     conflicts against the menu/editor world, and an editor command shadowed by a menu accel is
        //     flagged on the editor row too. At its default key an editor owner sits atop the matching
        //     SciDefault mirror row (same sciCmd -> benign), so this adds no spurious warning for defaults.
        for (const EditorEffective& e : store.editorAll())
        {
            if (e.effectiveRaw.empty() || keySpell::isChord(e.effectiveRaw)) continue;
            const NormKey nk = conflictKeys::fromAccelString(e.effectiveRaw);
            if (!nk.valid()) continue;
            KeyOwner o;
            o.kind       = OwnerKind::Editor;
            o.scope      = KeyScope::Editor;
            o.src        = e.overridden ? BindingSource::User : BindingSource::Default;
            o.sciCmd     = e.sciCmd;
            o.editorName = e.name;
            o.name       = editorCommandDisplayName(e.name);
            m_buckets[nk].push_back(std::move(o));
            m_editorKeys[e.name].push_back(nk);
        }

        // 3. Scoped hardcoded chords: the terminal chrome-escape keys (terminal_panel.h). Terminal scope,
        //    so the focus gate keeps them mutually exclusive with the menu bindings on the same keys
        //    (Edit>Move Line Up/Down) - the SCOPED_OK class exists exactly for this pair.
        addScoped(conflictKeys::fromWx(wxACCEL_CTRL | wxACCEL_SHIFT, WXK_UP),   KeyScope::Terminal);
        addScoped(conflictKeys::fromWx(wxACCEL_CTRL | wxACCEL_SHIFT, WXK_DOWN), KeyScope::Terminal);
    }

    // Worst conflict affecting the given menu command as currently bound. Looks up only the command's own
    // keys through the m_cmdKeys reverse index (this runs per-row in the mapper's grid refill, so a scan
    // of every bucket here made each keystroke O(rows x total-owners)).
    ConflictInfo forCommand(int cmdId) const
    {
        ConflictInfo worst;
        auto ki = m_cmdKeys.find(cmdId);
        if (ki == m_cmdKeys.end()) return worst;
        for (const NormKey& nk : ki->second)
        {
            auto it = m_buckets.find(nk);
            if (it == m_buckets.end()) continue;
            for (const KeyOwner& self : it->second)
            {
                if (self.kind != OwnerKind::Menu || self.cmdId != cmdId) continue;
                ConflictInfo info = classify(self, nk, it->second);
                if (severity(info.cls) > severity(worst.cls)) worst = info;
            }
        }
        return worst;
    }

    // Classify a PROPOSED binding (mapper capture): cmdId onto `nk` in `scope`, ignoring cmdId's own
    // existing menu owners (a rebind replaces them). `sciEquiv` is the command's SCI equivalence.
    ConflictInfo forProposed(int cmdId, const NormKey& nk, KeyScope scope) const
    {
        KeyOwner self;
        self.kind   = OwnerKind::Menu;
        self.scope  = scope;
        self.cmdId  = cmdId;
        self.sciCmd = sciEquivalentForMenuCmd(cmdId);
        auto it = m_buckets.find(nk);
        if (it == m_buckets.end()) { ConflictInfo none; none.key = nk; return none; }
        return classify(self, nk, it->second);
    }

    // Worst conflict affecting a curated editor command as currently bound (the mapper's Editor rows).
    // Looks up only the command's own effective key(s) via the m_editorKeys reverse index (same per-row
    // hot path as forCommand) - an editor command shadowed by a menu accel, or moved onto a key another
    // command already holds, surfaces here.
    ConflictInfo forEditor(const wxString& editorName) const
    {
        ConflictInfo worst;
        auto ki = m_editorKeys.find(editorName);
        if (ki == m_editorKeys.end()) return worst;
        for (const NormKey& nk : ki->second)
        {
            auto it = m_buckets.find(nk);
            if (it == m_buckets.end()) continue;
            for (const KeyOwner& self : it->second)
            {
                if (self.kind != OwnerKind::Editor || self.editorName != editorName) continue;
                ConflictInfo info = classify(self, nk, it->second);
                if (severity(info.cls) > severity(worst.cls)) worst = info;
            }
        }
        return worst;
    }

    // Classify a PROPOSED editor rebind (mapper capture): move `editorName`/`sciCmd` onto `nk` in the
    // Editor scope, ignoring its own existing owners (a rebind replaces them).
    ConflictInfo forProposedEditor(const wxString& editorName, int sciCmd, const NormKey& nk) const
    {
        KeyOwner self;
        self.kind       = OwnerKind::Editor;
        self.scope      = KeyScope::Editor;
        self.sciCmd     = sciCmd;
        self.editorName = editorName;
        auto it = m_buckets.find(nk);
        if (it == m_buckets.end()) { ConflictInfo none; none.key = nk; return none; }
        return classify(self, nk, it->second);
    }

    // Every menu command id whose current binding lands in a bucket also occupied by an owner that binds
    // `nk` - used by the mapper's "Record keys" reverse lookup to filter the grid to whatever binds a key.
    std::vector<int> menuCommandsBoundTo(const NormKey& nk) const
    {
        std::vector<int> out;
        auto it = m_buckets.find(nk);
        if (it == m_buckets.end()) return out;
        for (const KeyOwner& o : it->second)
            if (o.kind == OwnerKind::Menu) out.push_back(o.cmdId);
        return out;
    }

    // Every distinct key whose owners REALLY collide - the mapper's "Details..." report. Derived straight
    // from the existing buckets: each multi-owner bucket is classified pairwise (the same classifyPair the
    // per-command queries use) and kept when its worst pair is Hard or NonEquivShadow; the issue then
    // carries the WHOLE bucket so the report can name every owner on the key, coexisting ones included.
    // Sorted hard conflicts first, then warnings, chords alphabetical within a class.
    std::vector<ConflictIssue> allIssues() const
    {
        std::vector<ConflictIssue> out;
        for (const auto& kv : m_buckets)
        {
            const std::vector<KeyOwner>& bucket = kv.second;
            if (bucket.size() < 2) continue;
            ConflictClass worst = ConflictClass::None;
            for (size_t i = 0; i < bucket.size(); ++i)
                for (size_t j = i + 1; j < bucket.size(); ++j)
                {
                    const ConflictClass c = classifyPair(bucket[i], bucket[j]);
                    if (severity(c) > severity(worst)) worst = c;
                }
            if (worst != ConflictClass::Hard && worst != ConflictClass::NonEquivShadow) continue;
            ConflictIssue issue;
            issue.cls        = worst;
            issue.key        = kv.first;
            issue.keyDisplay = conflictKeys::display(kv.first);
            issue.owners     = bucket;
            for (KeyOwner& o : issue.owners)
                if (o.kind == OwnerKind::Menu) o.name = menuOwnerName(o);   // lazy label, memoized
            out.push_back(std::move(issue));
        }
        std::sort(out.begin(), out.end(), [](const ConflictIssue& a, const ConflictIssue& b){
            if (a.cls != b.cls) return severity(a.cls) > severity(b.cls);
            return a.keyDisplay.CmpNoCase(b.keyDisplay) < 0;
        });
        return out;
    }

    static int severity(ConflictClass c)
    {
        switch (c)
        {
            case ConflictClass::Hard:           return 4;
            case ConflictClass::NonEquivShadow: return 3;
            case ConflictClass::BenignShadow:   return 2;
            case ConflictClass::ScopedOk:       return 1;
            default:                            return 0;
        }
    }

    // Two scopes can be simultaneously active (so a same-key collision is real) when neither is Terminal
    // (Global and Editor both live under editor focus) or both are the same scope. A Terminal binding is
    // mutually exclusive with everything else because the focus gate empties the frame accel table while
    // the terminal is focused (plan 5.2), and Terminal keys only fire then.
    static bool scopesCanCollide(KeyScope a, KeyScope b)
    {
        if (a == KeyScope::Terminal || b == KeyScope::Terminal) return a == b;
        return true;
    }

private:
    void addScoped(const NormKey& nk, KeyScope scope)
    {
        if (!nk.valid()) return;
        KeyOwner o;
        o.kind  = OwnerKind::Scoped;
        o.scope = scope;
        o.name  = wxString();       // filled by the dialog if it wants a label; not needed for the class
        m_buckets[nk].push_back(std::move(o));
    }

    // Two owners are "the same action" (benign) only when both resolve to the same NON-ZERO SCI command.
    // A menu owner carries its SCI equivalent (kMenuSciEquivalence); a Scintilla default carries its own.
    // Requiring non-zero both sides stops two unrelated menu commands (sciCmd 0 each) from looking equal.
    static bool equivalent(const KeyOwner& a, const KeyOwner& b)
    {
        return a.sciCmd != 0 && b.sciCmd != 0 && a.sciCmd == b.sciCmd;
    }

    // A Scintilla default OR a curated editor command - a menu accel over either is a SHADOW, not a hard
    // conflict.
    static bool isEditorish(const KeyOwner& o) { return o.kind == OwnerKind::SciDefault || o.kind == OwnerKind::Editor; }

    // Classify ONE owner pair - the shared kernel of classify() (per-command queries) and allIssues()
    // (the details report), so the two can never disagree on what a collision means. None == a self pair:
    // the same menu command's own owner, or the same editor command's own owner (either as an Editor owner
    // or as its own SciDefault mirror row at the default key). Symmetric in its arguments.
    static ConflictClass classifyPair(const KeyOwner& a, const KeyOwner& b)
    {
        if (a.kind == OwnerKind::Menu && b.kind == OwnerKind::Menu && a.cmdId == b.cmdId)
            return ConflictClass::None;
        if (isEditorish(a) && isEditorish(b) && a.sciCmd != 0 && b.sciCmd == a.sciCmd)
            return ConflictClass::None;
        if (!scopesCanCollide(a.scope, b.scope)) return ConflictClass::ScopedOk;
        if (equivalent(a, b))                    return ConflictClass::BenignShadow;
        if ((a.kind == OwnerKind::Menu && isEditorish(b)) ||
            (isEditorish(a) && b.kind == OwnerKind::Menu))
                                                 return ConflictClass::NonEquivShadow;
        return ConflictClass::Hard;
    }

    ConflictInfo classify(const KeyOwner& self, const NormKey& nk, const std::vector<KeyOwner>& bucket) const
    {
        ConflictInfo worst;
        worst.key = nk;
        worst.selfScope = self.scope;
        for (const KeyOwner& other : bucket)
        {
            const ConflictClass c = classifyPair(self, other);   // None == a self pair, never worst

            if (severity(c) > severity(worst.cls))
            {
                worst.cls                 = c;
                worst.otherName           = (other.kind == OwnerKind::Menu) ? menuOwnerName(other) : other.name;
                worst.otherCmdId          = (other.kind == OwnerKind::Menu) ? other.cmdId : 0;
                worst.otherIsEditorDefault = isEditorish(other);
                worst.otherScope          = other.scope;
                // the "shadowed editor command" name: the SCI_* symbol for a mirror default, or the
                // (translated) editor-command label for a curated Editor owner - both read well in the
                // "%s shadows the built-in editor command %s" warning.
                worst.otherSciName        = isEditorish(other) ? other.name : wxString();
            }
        }
        return worst;
    }

    // Resolve a MENU owner's display label lazily, memoized per rebuild: the label is only needed when a
    // conflict actually names the command (the info line / the conflict prompts), so the FindItem walk
    // runs for those few commands instead of for every owner up front. An empty result (a command not on
    // the bar) falls back to the owner's stored symbolicName, mirroring the mapper's cmdName() fallback.
    wxString menuOwnerName(const KeyOwner& o) const
    {
        if (!m_menuName) return o.name;
        auto it = m_menuNameCache.find(o.cmdId);
        if (it == m_menuNameCache.end())
        {
            wxString n = m_menuName(o.cmdId);
            if (n.empty()) n = o.name;
            it = m_menuNameCache.emplace(o.cmdId, std::move(n)).first;
        }
        return it->second;
    }

    std::unordered_map<NormKey, std::vector<KeyOwner>, NormKeyHash> m_buckets;
    // Reverse indexes: which buckets hold a given command as a SELF owner - so forCommand()/forEditor()
    // visit only that command's own keys instead of scanning every bucket (they run per-row per-keystroke
    // in the mapper's grid refill).
    std::unordered_map<int, std::vector<NormKey>>                m_cmdKeys;
    std::unordered_map<wxString, std::vector<NormKey>, WxnKeyHash> m_editorKeys;
    std::function<wxString(int)>                  m_menuName;        // lazy menu-label resolver (rebuild)
    mutable std::unordered_map<int, wxString>     m_menuNameCache;   // cmdId -> resolved label, per rebuild
};
