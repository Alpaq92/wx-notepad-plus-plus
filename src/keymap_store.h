#pragma once
// =====================================================================
// KeymapStore - the shortcut merge engine.
//
// One store resolves an EFFECTIVE binding set from three layers, lowest precedence first:
//   Tier 0  wxnote.default   - compiled defaults harvested from MenuItemDef.defaultAccel (locale-
//                              independent DATA; a translator typo can no longer rebind a key).
//   Tier 1-2 scheme chain    - named, delta-only schemes resolved parent-first. The bundled read-only
//                              preset ("wxnote.default") lands in Phase 2
//                              (src/keymap_schemes.h); this header already resolves whatever schemes
//                              are registered / present in the file, so Phase 2 only has to register them.
//   Tier 3  user layer       - the bindings list of keybindings.yaml, applied last, highest precedence,
//                              surviving scheme switches (VS Code semantics).
//
// The result is keyed by each command's stable symbolicName (src/menu_model.h). It is applied to the
// frame's one wxAcceleratorTable and to the menu labels by WxnShellFrame::refreshAccelerators().
//
// The model, keybindings.yaml's load/resolve/save, and the schemes. The file is the user's to edit by
// hand too; wxNote writes it only when the Shortcut Mapper (shortcut_mapper_dialog.h) is closed with OK
// or a plugin commits a scheme (nib.keymap) - never on exit.
// =====================================================================
#include <wx/string.h>
#include <wx/accel.h>
#include <wx/file.h>
#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/log.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <functional>
#include "yaml_io.h"

// std::unordered_map with a wxString key: hash via the UTF-8 bytes so we don't depend on whether this
// wx build ships a usable std::hash<wxString> / WxnKeyHash for the unordered_map Hash slot. Equality
// uses wxString::operator== (std::equal_to default). ASCII symbolic names make this cheap.
struct WxnKeyHash { size_t operator()(const wxString& s) const { return std::hash<std::string>()(std::string(s.utf8_str())); } };

// ----- key spelling: human-readable, locale-proof -----------------------------------------
namespace keySpell
{
    // Canonical COMPARISON key: parse with wxAcceleratorEntry, re-emit with ToRawString() (invariant
    // English tokens - NOT the localized ToString(), whose spelling can reparse wrong across the 8 UI
    // languages or break on a locale switch), then lowercase. Idempotent. Used only for dedup/unbind
    // matching, never for display. A string wx cannot parse falls back to a lowercased literal so a bad
    // hand-edit is still comparable rather than crashing.
    inline wxString canonical(const wxString& s)
    {
        wxAcceleratorEntry e;
        if (s.empty() || !e.FromString(s)) return s.Lower();
        return e.ToRawString().Lower();
    }
    // A single-level chord is two space-separated keystrokes ("ctrl+k ctrl+c"). wxAcceleratorTable can't
    // express these, so the store partitions them out for the CHAR_HOOK layer. None exist in
    // the Phase 1 defaults.
    inline bool isChord(const wxString& s) { return s.Find(' ') != wxNOT_FOUND; }

    // Localized DISPLAY spelling of one raw accel - for the Shortcut Mapper and the Command Palette,
    // which would otherwise each grow their own copy. ToString() is locale-aware and DISPLAY-ONLY; the
    // persisted spelling stays ToRawString() (see canonical() above), or a UI-language switch would
    // silently rewrite the user's keymap into a spelling the next locale cannot reparse.
    inline wxString displayAccel(const wxString& raw)
    {
        if (raw.empty()) return wxString();
        if (isChord(raw)) return raw;      // wxAcceleratorEntry cannot express a chord
        wxAcceleratorEntry e;
        if (e.FromString(raw)) { const wxString s = e.ToString(); if (!s.empty()) return s; }
        return raw;
    }
}

// ----- public model -----------------------------------------------------------------------
enum class BindingSource { Default, Scheme, User };
enum class KeyScope      { Global, Editor, Terminal };   // + FindBar reserved for later phases

struct EffectiveAccel
{
    wxString raw;                       // the string to DISPLAY and to feed wxAcceleratorEntry::FromString.
                                        // For a Tier-0 default this is the authored spelling verbatim
                                        // ("Ctrl+S", "Del") so the rewritten menu label is byte-identical
                                        // to the pre-refactor label; for a scheme/user override it is the
                                        // ToRawString() normalization of the keybindings.yaml key.
    KeyScope scope   = KeyScope::Global;
    bool     isChord = false;
};

struct EffectiveBinding
{
    wxString symbolicName;
    int      cmdId = 0;
    std::vector<EffectiveAccel> accels;             // 0..n
    BindingSource source = BindingSource::Default;
    wxString sourceScheme;                          // set when source == Scheme

    bool hasPlainGlobal() const                     // a usable plain (non-chord) Global accel exists?
    {
        for (const auto& a : accels)
            if (!a.isChord && a.scope == KeyScope::Global && !a.raw.empty()) return true;
        return false;
    }
    // The accel to show on the menu item / install in the frame table (the first plain-global one),
    // or empty if the command is unbound.
    wxString primaryRaw() const
    {
        for (const auto& a : accels)
            if (!a.isChord && a.scope == KeyScope::Global && !a.raw.empty()) return a.raw;
        return wxString();
    }
    // Every plain (non-chord) Global accel AFTER the primary: the dual-default rows
    // (addDefaultSecondary - redo Ctrl+Shift+Z, close Ctrl+F4) and user bare-bind ADDs. The
    // borderless frame's accel table installs these directly (collectFrameAccels walks ALL accels),
    // but the NATIVE menubar derives exactly ONE accelerator per item from the "\t<primary>" label
    // suffix - so rewriteMenuAccelLabels must install THIS list via wxMenuItem::AddExtraAccel or the
    // secondaries silently don't fire there. One derivation, shared with keymap_selftest's mirror,
    // so the two frame paths can be asserted against the same source.
    std::vector<wxString> secondaryRaws() const
    {
        std::vector<wxString> out;
        bool first = true;
        for (const auto& a : accels)
        {
            if (a.isChord || a.scope != KeyScope::Global || a.raw.empty()) continue;
            if (first) { first = false; continue; }   // the primary lives on the menu label
            out.push_back(a.raw);
        }
        return out;
    }
};

// A single override row (a rule of the user layer, or a scheme delta). A leading '-' on the command marks
// an unbind; an empty key on an unbind drops ALL inherited accels, otherwise only the matching one.
struct KeymapDelta
{
    wxString symbolicName;
    wxString key;                       // canonical stored form; empty allowed only for a bare unbind
    KeyScope scope  = KeyScope::Global;
    bool     unbind = false;
};

struct KeymapScheme
{
    wxString id;
    wxString name;
    wxString parent;                    // id this scheme deltas against ("" / unknown => root only)
    std::vector<KeymapDelta> deltas;
    bool     bundled = false;           // true for compiled read-only presets (registered in Phase 2)
    // EDITOR-tier deltas (the curated "editor.*" commands), scheme-scoped like `deltas` is for the
    // menu tier: inert while the scheme is inactive, applied by resolveEditor() when the scheme is in
    // the active chain, persisted with the scheme (a nib.keymap commit with activate=0 - e.g. the
    // npp-compat import - stores its ScintillaKeys here so a later activation applies them).
    // Appended LAST so any positional KeymapScheme initializer that stops earlier stays valid.
    std::vector<KeymapDelta> editorDeltas;
};

// ----- the Scintilla editor tier (Phase 4) -------------------------------------------------
// Editor commands are a SEPARATE space from menu commands: they are keyed by a stable ascii name
// ("editor.lineCut"), carry a Scintilla SCI_* id instead of a menu command id, live only in KeyScope
// Editor, and persist in keybindings.yaml's one bindings list, told apart by their "editor." prefix. The
// store resolves them like the menu tiers - a compiled default (seeded from src/shortcut_labels.h) that a
// user override replaces - but keeps them apart so a menu delta can never touch an editor row and vice
// versa, and so the app can apply them through Scintilla's CmdKeyAssign rather than the frame accel table.
struct EditorEffective
{
    wxString name;                      // stable ascii key ("editor.lineCut")
    int      sciCmd = 0;                // SCI_* command this row remaps
    wxString effectiveRaw;              // effective accel string (default OR override); empty => unbound
    bool     overridden = false;        // an override is in effect: the flat user layer (rebind or
                                        // explicit clear) or an ACTIVE scheme's editor delta - i.e.
                                        // "not the stock default; the app must manage this key"
};

class KeymapStore
{
public:
    static constexpr int kCurrentVersion = 1;

    // ---- Tier 0 seeding: one call per menu command carrying a symbolicName (see menu_builder.h's
    // seedKeymapDefaults). defaultAccelRaw is the authored spelling or empty. Insertion order is menu
    // order and is preserved by all() for a stable future mapper grid.
    void addDefault(const wxString& sym, int cmdId, const wxString& defaultAccelRaw)
    {
        if (sym.empty()) return;
        auto it = m_rootIndex.find(sym);
        if (it != m_rootIndex.end())                 // duplicate symbolicName: keep the first, but adopt
        {                                            // an accel if the first had none (defensive; shouldn't happen)
            RootEntry& e = m_root[it->second];
            if (e.defaultAccel.empty() && !defaultAccelRaw.empty()) { e.defaultAccel = defaultAccelRaw; e.cmdId = cmdId; }
            return;
        }
        m_rootIndex[sym] = m_root.size();
        m_root.push_back({ sym, cmdId, defaultAccelRaw });
    }

    // Re-point an already-seeded symbolicName at a DIFFERENT command id, keeping its accel and any user
    // override. addDefault deliberately cannot do this (it keeps the first entry and returns), which is
    // right for menu commands - their ids are compile-time constants, so a second addDefault for the same
    // sym is a bug to ignore rather than honour. Saved macros are the one case where the mapping genuinely
    // moves at runtime: the binding key is the macro's uid ("macro.<uid>", stable across rename/reorder)
    // but the command id is the macro's POSITION in the Macro menu (myID_MACRO_ITEM + index), so deleting
    // or reordering a macro shifts ids under bindings that must not move. Callers re-point every affected
    // row and then resolveAll(). Unknown sym: ignored, so callers need not pre-check.
    void remapCmdId(const wxString& sym, int cmdId)
    {
        auto it = m_rootIndex.find(sym);
        if (it == m_rootIndex.end()) return;
        m_root[it->second].cmdId = cmdId;
    }

    // Drop a Tier-0 row entirely, plus any user override keyed to it - for a macro the user deleted, whose
    // "macro.<uid>" can never be seeded again (uids are monotonic and never reused). Without this the row
    // would linger in keybindings.yaml and in the Shortcut Mapper as a binding for a macro that is gone.
    void removeDefault(const wxString& sym)
    {
        auto it = m_rootIndex.find(sym);
        if (it == m_rootIndex.end()) return;
        const size_t at = it->second;
        m_root.erase(m_root.begin() + static_cast<ptrdiff_t>(at));
        m_rootIndex.erase(it);
        for (auto& kv : m_rootIndex) if (kv.second > at) --kv.second;   // indices after the hole shift down
        m_userLayer.erase(std::remove_if(m_userLayer.begin(), m_userLayer.end(),
                                         [&](const KeymapDelta& d) { return d.symbolicName == sym; }),
                          m_userLayer.end());
    }

    // An ADDITIONAL Tier-0 default accel for an already-seeded command (the "bind both" consensus rows:
    // redo = Ctrl+Y AND Ctrl+Shift+Z, close tab = Ctrl+W AND Ctrl+F4 - see menu_builder.h's
    // kSecondaryDefaults). Mirrors the user layer's bare-bind ADD semantics: the accel rides along
    // BESIDE the primary, it never replaces it, and the menu label keeps showing the primary only
    // (primaryRaw() returns the first accel; resolveAll appends secondaries after it). A rebind/unbind
    // delta clears secondaries together with the primary (pushReplace's bare unbind wipes all inherited
    // accels), so the mapper's single-key replace semantics are unchanged. Unknown sym: ignored.
    void addDefaultSecondary(const wxString& sym, const wxString& accelRaw)
    {
        if (sym.empty() || accelRaw.empty()) return;
        auto it = m_rootIndex.find(sym);
        if (it == m_rootIndex.end()) return;
        RootEntry& e = m_root[it->second];
        const wxString k = keySpell::canonical(accelRaw);
        if (keySpell::canonical(e.defaultAccel) == k) return;              // duplicate of the primary
        for (const wxString& s : e.secondary)
            if (keySpell::canonical(s) == k) return;                       // already added
        e.secondary.push_back(accelRaw);
    }

    // Register a scheme (a user scheme parsed from the file, or a bundled preset from Phase 2). Replaces
    // any scheme with the same id (idempotent re-register / re-import). REFUSES (returns false) a
    // NON-bundled scheme whose id names a bundled read-only preset (e.g. "wxnote.default"):
    // the replace would swap the compiled preset's curated deltas for the caller's data while the
    // sticky bundled flag kept it read-only and un-serialized - silent, unrepairable corruption. This
    // is the same hazard duplicateScheme guards, centralized here so BOTH untrusted writers (a
    // hand-edited keybindings.yaml via parseInto, and a plugin via the nib.keymap commit) are covered.
    bool registerScheme(const KeymapScheme& s)
    {
        if (!s.bundled && schemeIsBundled(s.id)) return false;   // reserved preset id (also rejects "")
        for (auto& existing : m_schemes)
            if (existing.id == s.id) { bool b = existing.bundled; existing = s; existing.bundled = existing.bundled || b; return true; }
        m_schemes.push_back(s);
        return true;
    }

    // ---- load / save ------------------------------------------------------------------------------
    // Best-effort, wxLogNull-wrapped (a bad hand-edit or unreadable file must never pop a dialog at
    // startup - mirrors the contextmenu.yaml load pattern). Populates the active-scheme pointer, the user
    // layer, and any user schemes, then resolves. Writes nothing.
    void load(const wxString& userDataDir)
    {
        m_filePath = userDataDir + wxFILE_SEP_PATH + "keybindings.yaml";
        m_userLayer.clear();
        m_editorUser.clear();
        // drop previously-loaded NON-bundled schemes so a reload is clean; keep bundled presets
        m_schemes.erase(std::remove_if(m_schemes.begin(), m_schemes.end(),
                                       [](const KeymapScheme& s){ return !s.bundled; }), m_schemes.end());
        m_activeScheme = "wxnote.default";
        m_readOnly = false;
        m_loadError.clear();
        m_file = wxnyaml::Doc();
        m_keptTop.clear();
        m_keptRules.clear();
        m_keptSchemes.clear();

        wxLogNull noLog;
        if (wxFileExists(m_filePath))
        {
            wxFile f(m_filePath, wxFile::read);
            if (f.IsOpened())
            {
                wxString raw;
                if (f.ReadAll(&raw, wxConvUTF8) || f.ReadAll(&raw))
                    parseInto(std::string(raw.utf8_str()));
            }
        }
        // A DANGLING activeScheme - one naming a scheme neither registered (bundled/plugin) nor defined
        // in the file itself, such as a plugin's scheme once the plugin is gone - must not stick:
        // resolution would already fall back to the default chain (activeSchemeChain() stops at an unknown
        // id), but the id would be re-serialized by every save() and dangle forever, and the mapper's
        // picker could never show the real selection. Snap it back to the root HERE, after parseInto has
        // registered the file's own schemes, so an id defined later in the same file still resolves and
        // only a truly unknown one is dropped.
        if (m_activeScheme != "wxnote.default" && !schemeById(m_activeScheme))
            m_activeScheme = "wxnote.default";
        resolveAll();   // also rebuilds the editor tier (resolveAll ends in resolveEditor)
    }

    // Write keybindings.yaml (on the Shortcut Mapper's OK, or a plugin's scheme commit - never on exit). Refuses to write
    // over a file that does not parse, or one a newer wxNote wrote (version > current), so neither the
    // user's text nor unknown fields are clobbered.
    bool save() const
    {
        if (m_readOnly || m_filePath.empty()) return false;
        wxLogNull noLog;
        wxFileName::Mkdir(wxFileName(m_filePath).GetPath(), wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
        std::string out = serialize();
        // Atomic replace: fully write a sibling temp, flush it, then rename over the target. A plain
        // wxFile::write on m_filePath O_TRUNCs the existing keybindings.yaml BEFORE writing, so a crash /
        // power loss / full disk mid-write leaves a truncated or empty file; callers ignore save()'s
        // bool, so on next launch parseInto() fails on the corrupt file and the user's entire custom
        // keymap silently reverts to Tier-0 defaults. Writing a complete temp and renaming means any
        // failure leaves the previous good file untouched.
        const wxString tmp = m_filePath + ".tmp";
        {
            wxFile f(tmp, wxFile::write);
            if (!f.IsOpened()) return false;
            if (f.Write(out.data(), out.size()) != out.size()) { f.Close(); wxRemoveFile(tmp); return false; }
            if (!f.Flush())                                     { f.Close(); wxRemoveFile(tmp); return false; }
        }   // f destructor closes the fd before the rename below
        if (!wxRenameFile(tmp, m_filePath, /*overwrite*/ true)) { wxRemoveFile(tmp); return false; }
        return true;
    }

    // ---- resolution --------------------------------------------------------------------
    void resolveAll()
    {
        m_eff.clear();
        m_effOrder.clear();
        m_cmdToSym.clear();

        // Tier 0: wxnote.default
        for (const RootEntry& r : m_root)
        {
            EffectiveBinding b;
            b.symbolicName = r.sym;
            b.cmdId        = r.cmdId;
            b.source       = BindingSource::Default;
            b.sourceScheme = "wxnote.default";
            if (!r.defaultAccel.empty())
                b.accels.push_back({ r.defaultAccel, KeyScope::Global, keySpell::isChord(r.defaultAccel) });
            for (const wxString& s : r.secondary)   // additional defaults AFTER the primary, so
                b.accels.push_back({ s, KeyScope::Global, keySpell::isChord(s) });   // primaryRaw() (the
                // menu-label accel) keeps returning the authored primary; addDefaultSecondary deduped.
            m_effOrder.push_back(r.sym);
            m_eff.emplace(r.sym, std::move(b));
        }

        // Tier 1-2: the active scheme's parent chain, root -> ... -> active (excluding the root itself).
        for (const KeymapScheme* s : activeSchemeChain())
            for (const KeymapDelta& d : s->deltas)
                applyDelta(d, BindingSource::Scheme, s->name);

        // Tier 3: user layer, always last.
        for (const KeymapDelta& d : m_userLayer)
            applyDelta(d, BindingSource::User, wxString());

        // cmdId -> symbolicName index for the frame's per-menu-item lookup. Prefer an accel-bearing
        // binding on the (unlikely) event two symbolicNames share a cmdId, so accelerator behavior wins.
        for (const wxString& sym : m_effOrder)
        {
            const EffectiveBinding& b = m_eff.at(sym);
            if (b.cmdId == 0) continue;
            auto it = m_cmdToSym.find(b.cmdId);
            if (it == m_cmdToSym.end()) { m_cmdToSym[b.cmdId] = sym; continue; }
            if (!b.accels.empty() && m_eff.at(it->second).accels.empty()) it->second = sym;
        }

        // The editor tier also depends on the active scheme chain (a scheme's editorDeltas), so every
        // full re-resolve refreshes it too. This keeps one invariant for every caller - scheme
        // switch/registration, load, user-layer edits: after resolveAll() BOTH tiers are current.
        // Cheap (the curated editor set is ~2 dozen rows).
        resolveEditor();
    }

    // ---- lookups ----------------------------------------------------------------------------------
    const EffectiveBinding* effective(const wxString& sym) const
    {
        auto it = m_eff.find(sym);
        return it == m_eff.end() ? nullptr : &it->second;
    }
    const EffectiveBinding* effectiveByCmd(int cmdId) const
    {
        auto it = m_cmdToSym.find(cmdId);
        return it == m_cmdToSym.end() ? nullptr : effective(it->second);
    }
    std::vector<const EffectiveBinding*> all() const
    {
        std::vector<const EffectiveBinding*> out;
        out.reserve(m_effOrder.size());
        for (const wxString& sym : m_effOrder) out.push_back(&m_eff.at(sym));
        return out;
    }
    bool empty() const { return m_root.empty(); }

    // ---- mutation (feeds the user layer; the mapper's default write target - Phase 3) --------------
    // The ONE encoding of the "replace = unbind-then-bind" delta convention: append a full-unbind
    // FOLLOWED BY the new bind into any delta list. The unbind is load-bearing - without it the new
    // accel is ADDED alongside any inherited Tier-0/scheme default (the command would answer to two
    // keys); with it, resolution clears the inherited accels first, so the command ends up bound to
    // exactly the one key picked. Shared by rebind() (user layer) and the nib.keymap commit path (a
    // plugin scheme's deltas - src/main.cpp), so the convention has a single owner. `key` is taken
    // as prepared by the caller (rebind canonicalizes; the nib path passes its validated spelling).
    static void pushReplace(std::vector<KeymapDelta>& out, const wxString& sym,
                            const wxString& key, KeyScope scope = KeyScope::Global)
    {
        out.push_back({ sym, wxString(), KeyScope::Global, true });   // unbind inherited accels
        out.push_back({ sym, key, scope, false });                    // then bind the new one
    }
    // A clean single rebind: drop any stale user entries for this command, then write the
    // unbind-then-bind pair (see pushReplace). (Deliberate multi-binding is a separate, explicit
    // workflow, not a rebind.)
    void rebind(const wxString& sym, const wxString& key, KeyScope scope = KeyScope::Global)
    {
        dropUserEntries(sym);
        pushReplace(m_userLayer, sym, keySpell::canonical(key), scope);
        resolveAll();
    }
    void unbind(const wxString& sym, const wxString& key = wxEmptyString)
    {
        m_userLayer.push_back({ sym, key.empty() ? wxString() : keySpell::canonical(key), KeyScope::Global, true });
        resolveAll();
    }
    // The mapper's hard-conflict "Reassign" (steal) as ONE store transaction: drop ONLY `key` from
    // `stealFromSym` (a KEYED unbind - any other accels that command holds survive the steal; a bare
    // unbind here used to wipe them all), then give `sym` the clean unbind-then-bind replace. One
    // resolveAll() for the whole edit instead of one per mutator call.
    void reassignRebind(const wxString& stealFromSym, const wxString& sym, const wxString& key,
                        KeyScope scope = KeyScope::Global)
    {
        const wxString k = keySpell::canonical(key);
        m_userLayer.push_back({ stealFromSym, k, KeyScope::Global, true });   // keyed: only the colliding accel
        dropUserEntries(sym);
        pushReplace(m_userLayer, sym, k, scope);
        resolveAll();
    }
    // "Reset to default" (mapper): drop this command's USER-layer overrides so its effective binding falls
    // back to whatever the active scheme chain / Tier 0 default provides. Does NOT touch a bundled preset
    // (they are never mutated in place) nor a user scheme's own deltas - only the floating user layer, which
    // is where casual rebinds/clears land (VS Code mode). Idempotent if there was nothing to drop.
    void resetToDefault(const wxString& sym)
    {
        dropUserEntries(sym);
        resolveAll();
    }
    void setActiveScheme(const wxString& id) { m_activeScheme = id.empty() ? wxString("wxnote.default") : id; resolveAll(); }
    const wxString& activeScheme() const     { return m_activeScheme; }
    bool isReadOnly() const                  { return m_readOnly; }
    // Why keybindings.yaml could not be used ("" when it could, or does not exist). Such a file is left
    // alone - read-only, like a newer version's - and the defaults apply until it is fixed.
    const wxString& loadError() const        { return m_loadError; }

    // ---- scheme picker support (Phase 2 entry point; the full mapper UI is Phase 3) ---------------
    // Every registered scheme in registration order: bundled read-only presets first (wxnote.default -
    // registerKeymapSchemes runs before load()), then any user schemes from the file. A
    // picker renders this list; selecting one is setActiveScheme(id).
    std::vector<const KeymapScheme*> schemes() const
    {
        std::vector<const KeymapScheme*> out;
        out.reserve(m_schemes.size());
        for (const KeymapScheme& s : m_schemes) out.push_back(&s);
        return out;
    }
    // Is `id` a compiled read-only preset? The mapper uses this to choose between the two write
    // workflows: editing a bundled preset must copy-on-write (duplicateScheme); editing a user
    // scheme or the user layer writes in place. "wxnote.default" is read-only even if never registered.
    bool schemeIsBundled(const wxString& id) const
    {
        if (id.empty() || id == "wxnote.default") return true;
        const KeymapScheme* s = schemeById(id);
        return s ? s->bundled : false;
    }

    // Copy-on-write ("Duplicate scheme..."): clone `sourceId` into a NEW user scheme whose parent
    // is sourceId, so the clone inherits every effective binding through the parent chain but stores only
    // its OWN future edits as deltas (delta-only). bundled=false, so save() persists it and the read-only
    // source preset is never mutated. Rejects a newId that would shadow an existing scheme (which would
    // corrupt a preset via registerScheme's bundled-flag merge). Optionally switches to the clone. Returns
    // the new id, or empty on failure.
    wxString duplicateScheme(const wxString& sourceId, const wxString& newId, const wxString& newName, bool activate = true)
    {
        if (newId.empty() || schemeById(newId)) return wxString();
        KeymapScheme s;
        s.id      = newId;
        s.name    = newName.empty() ? newId : newName;
        s.parent  = sourceId;      // delta-only: everything comes from the source chain; only new edits land here
        s.bundled = false;         // writable + serialized by save()
        if (!registerScheme(s)) return wxString();   // reserved preset id (e.g. an unregistered "wxnote.default")
        if (activate) m_activeScheme = newId;
        resolveAll();
        return newId;
    }

    // ---- editor tier -------------------------------------------------------------------
    // Seed one curated editor command's Tier-0 default (called once per row from seedEditorKeymapDefaults
    // in shortcut_labels.h, before load()). Insertion order is the mapper's Editor-row order. A duplicate
    // name keeps the first (defensive). Mirrors addDefault: seeding does NOT resolve - the caller runs
    // load()/resolveAll() once after the seed loop (the per-row full rebuild here used to make seeding
    // O(n^2) for work load() immediately redid anyway).
    void addEditorDefault(const wxString& name, int sciCmd, const wxString& defaultAccelRaw)
    {
        if (name.empty()) return;
        if (m_editorIndex.find(name) != m_editorIndex.end()) return;
        m_editorIndex[name] = m_editorRoot.size();
        m_editorRoot.push_back({ name, sciCmd, defaultAccelRaw });
    }
    // Rebind an editor command to `key` (the mapper's Modify), or CLEAR it when key is empty (the mapper's
    // Clear -> an explicit unbind that persists, so a future default can't silently reappear). Writes to
    // the editor user layer; canonicalized like menu keys.
    void setEditorBinding(const wxString& name, const wxString& key)
    {
        if (name.empty()) return;
        m_editorUser[name] = key.empty() ? wxString() : keySpell::canonical(key);
        resolveEditor();
    }
    // "Reset to default": drop this editor command's user override so it falls back to the Tier-0 default.
    void resetEditorToDefault(const wxString& name)
    {
        m_editorUser.erase(name);
        resolveEditor();
    }
    const EditorEffective* editorEffective(const wxString& name) const
    {
        auto it = m_editorEffIndex.find(name);
        return it == m_editorEffIndex.end() ? nullptr : &m_editorEff[it->second];
    }
    // Every editor command in seed order (the mapper's Editor rows; applyEditorKeymap's apply list).
    const std::vector<EditorEffective>& editorAll() const { return m_editorEff; }

private:
    struct RootEntry
    {
        wxString sym; int cmdId; wxString defaultAccel;
        std::vector<wxString> secondary;   // additional Tier-0 defaults (addDefaultSecondary) - resolved
                                           // after the primary, never shown as the menu-label accel
    };
    struct EditorRootEntry { wxString name; int sciCmd; wxString defaultAccel; };

    void dropUserEntries(const wxString& sym)
    {
        m_userLayer.erase(std::remove_if(m_userLayer.begin(), m_userLayer.end(),
                          [&](const KeymapDelta& d){ return d.symbolicName == sym; }), m_userLayer.end());
    }

    // Rebuild the editor effective set. Precedence mirrors the menu tiers, lowest first:
    //   Tier 0  curated editor roots (defaults)
    //   Tier 1-2 the ACTIVE SCHEME CHAIN's editorDeltas, parent-first, last-wins per name (e.g. an
    //            imported Notepad++ scheme's ScintillaKeys - inert until that scheme is activated)
    //   Tier 3  the flat editor user layer, always on top (VS Code semantics)
    // An override with an empty value is an explicit clear -> unbound. Cheap; called from resolveAll()
    // and on every editor mutation.
    void resolveEditor()
    {
        // scheme tier: name -> key ("" == cleared). The parent-first chain walk makes a child scheme's
        // delta for the same name win, exactly like applyDelta ordering on the menu tier.
        std::unordered_map<wxString, wxString, WxnKeyHash> schemeOv;
        for (const KeymapScheme* s : activeSchemeChain())
            for (const KeymapDelta& d : s->editorDeltas)
                schemeOv[d.symbolicName] = d.unbind ? wxString() : keySpell::canonical(d.key);

        m_editorEff.clear();
        m_editorEffIndex.clear();
        m_editorEff.reserve(m_editorRoot.size());
        for (const EditorRootEntry& r : m_editorRoot)
        {
            EditorEffective e;
            e.name   = r.name;
            e.sciCmd = r.sciCmd;
            const wxString* ov = nullptr;
            auto uit = m_editorUser.find(r.name);
            if (uit != m_editorUser.end()) ov = &uit->second;         // user layer wins
            else
            {
                auto sit = schemeOv.find(r.name);
                if (sit != schemeOv.end()) ov = &sit->second;         // active scheme chain
            }
            if (ov)                           // override (possibly an explicit clear)
            {
                e.overridden   = true;
                e.effectiveRaw = ov->empty() ? wxString() : normalizeForDisplay(*ov);
            }
            else                              // Tier-0 default, authored spelling verbatim
            {
                e.overridden   = false;
                e.effectiveRaw = r.defaultAccel;
            }
            m_editorEffIndex[r.name] = m_editorEff.size();
            m_editorEff.push_back(std::move(e));
        }
    }

    // active "my-keys"(parent "base") -> [ base, my-keys ]; root and unknown parents terminate.
    std::vector<const KeymapScheme*> activeSchemeChain() const
    {
        std::vector<const KeymapScheme*> chain;
        wxString id = m_activeScheme;
        // guard against a malformed parent cycle in a hand-edited file
        for (int guard = 0; guard < 64 && !id.empty() && id != "wxnote.default"; ++guard)
        {
            const KeymapScheme* s = schemeById(id);
            if (!s) break;
            chain.insert(chain.begin(), s);
            id = s->parent;
        }
        return chain;
    }
    const KeymapScheme* schemeById(const wxString& id) const
    {
        for (const KeymapScheme& s : m_schemes) if (s.id == id) return &s;
        return nullptr;
    }

    void applyDelta(const KeymapDelta& d, BindingSource src, const wxString& schemeName)
    {
        auto it = m_eff.find(d.symbolicName);
        if (it == m_eff.end()) return;              // unknown symbolicName: retained-but-ignored (fwd-compat)
        EffectiveBinding& b = it->second;
        if (d.unbind)
        {
            if (d.key.empty()) b.accels.clear();     // "-command"
            else                                     // "-command" + key: drop only the matching accel
            {
                const wxString k = keySpell::canonical(d.key);
                b.accels.erase(std::remove_if(b.accels.begin(), b.accels.end(),
                               [&](const EffectiveAccel& a){ return keySpell::canonical(a.raw) == k; }), b.accels.end());
            }
        }
        else
        {
            const wxString norm = normalizeForDisplay(d.key);
            const wxString k    = keySpell::canonical(d.key);
            bool present = false;
            for (const auto& a : b.accels) if (keySpell::canonical(a.raw) == k) { present = true; break; }
            if (!present) b.accels.push_back({ norm, d.scope, keySpell::isChord(d.key) });
        }
        b.source = src;
        b.sourceScheme = schemeName;
    }

    // A user/scheme override's display form: the invariant ToRawString() spelling ("Ctrl+Alt+S"), so it
    // reads normally on the menu regardless of the (possibly lowercase) hand-edited key. Chords are left
    // verbatim (FromString can't round-trip them).
    static wxString normalizeForDisplay(const wxString& key)
    {
        if (keySpell::isChord(key)) return key;
        wxAcceleratorEntry e;
        if (!e.FromString(key)) return key;
        return e.ToRawString();
    }

    // ================= keybindings.yaml =================
    // Read and written through src/yaml_io.h (rapidyaml): untrusted input degrades to "ignore and keep
    // resolving", never a throw or a crash - a bad hand edit must not brick startup.
    //
    //   version: 1
    //   scheme: wxnote.default
    //   bindings:                                   # the user layer, applied last; a later rule wins
    //     - {key: ctrl+shift+a, command: file.saveAll}
    //     - {command: -view.tab.tab9}               # '-': remove the key (or, without one, every key)
    //     - {key: ctrl+shift+x, command: editor.lineCut}
    //   schemes:
    //     - {id: my.keys, name: My keys, parent: wxnote.default, bindings: [...]}
    //
    // One list holds both tiers: an "editor.*" command is a Scintilla editor command (resolveEditor), any
    // other a menu command - no menu command's name starts with "editor.", so the prefix cannot misroute.
    static bool isEditorCommand(const wxString& name) { return name.StartsWith("editor."); }

    static KeyScope scopeFromStr(const wxString& w)
    {
        if (w.IsSameAs("editor",   false)) return KeyScope::Editor;
        if (w.IsSameAs("terminal", false)) return KeyScope::Terminal;
        return KeyScope::Global;
    }
    static const char* scopeToStr(KeyScope s)
    {
        return s == KeyScope::Editor ? "editor" : s == KeyScope::Terminal ? "terminal" : "global";
    }
    static wxString fromUtf8(const std::string& s) { return wxString::FromUTF8(s.c_str()); }

    // One rule: {key, command, when}. A leading '-' on the command removes keys instead of adding one.
    // False for a rule this build cannot read - no command, an adding rule with no key, a `when` it does
    // not know (a condition from a newer build must not quietly turn global): parseInto keeps those as
    // they are written, so a save puts them back.
    static bool deltaFromYaml(wxnyaml::Node e, KeymapDelta& out)
    {
        std::string command;
        if (!wxnyaml::isMap(e) || !wxnyaml::getText(wxnyaml::child(e, "command"), command)) return false;
        wxString c = fromUtf8(command);
        out.unbind = c.StartsWith("-");
        if (out.unbind) c = c.Mid(1);
        if (c.empty()) return false;
        out.symbolicName = c;
        out.key = fromUtf8(wxnyaml::textOr(wxnyaml::child(e, "key"), std::string()));
        out.scope = KeyScope::Global;
        const wxnyaml::Node when = wxnyaml::child(e, "when");
        if (when.readable())
        {
            std::string w;
            if (!wxnyaml::getText(when, w)) return false;
            const wxString ws = fromUtf8(w);
            if (!ws.IsSameAs("global", false) && !ws.IsSameAs("editor", false) && !ws.IsSameAs("terminal", false)) return false;
            out.scope = scopeFromStr(ws);
        }
        return !(out.unbind == false && out.key.empty());   // a rule that adds must name the key
    }

    void parseInto(const std::string& text)
    {
        wxnyaml::Doc& doc = m_file;   // kept: the entries this build cannot read are written back from it
        const bool parsed = wxnyaml::parse(text, doc, "keybindings.yaml");
        const wxnyaml::Node root = doc.root();
        if (parsed && !root.readable()) return;   // empty, or only comments: no changes to apply
        if (!parsed || !wxnyaml::isMap(root))
        {
            // Not usable: the defaults apply, and nothing is saved until the file is fixed - writing this
            // session's changes would throw away everything the user had written in it.
            m_readOnly = true;
            m_loadError = parsed ? wxString("expected version:, scheme: and bindings: at the top level")
                                 : fromUtf8(doc.error);
            return;
        }

        long long version = 0;
        if (wxnyaml::getInteger(wxnyaml::child(root, "version"), version) && version > kCurrentVersion)
            m_readOnly = true;   // a newer wxNote wrote it: don't clobber what this build cannot represent

        std::string scheme;
        if (wxnyaml::getText(wxnyaml::child(root, "scheme"), scheme) && !scheme.empty()) m_activeScheme = fromUtf8(scheme);

        for (wxnyaml::Node c : root.children())   // a top-level entry this build does not know: kept
        {
            const std::string k = wxnyaml::keyOf(c);
            if (k != "version" && k != "scheme" && k != "bindings" && k != "schemes") m_keptTop.push_back(c.id());
        }

        const wxnyaml::Node bindings = wxnyaml::child(root, "bindings");
        if (wxnyaml::isSeq(bindings))
            for (wxnyaml::Node e : bindings.children())
            {
                KeymapDelta d;
                if (!deltaFromYaml(e, d)) { m_keptRules.push_back({ wxString(), e.id() }); continue; }
                // The editor tier: {key: ctrl+k, command: editor.lineCut} rebinds; "-editor.lineCut" (key
                // optional) is an explicit CLEAR. Kept in its own map, applied to the curated editor roots by
                // resolveEditor(); a name this build doesn't define is retained but ignored.
                if (isEditorCommand(d.symbolicName)) m_editorUser[d.symbolicName] = d.unbind ? wxString() : keySpell::canonical(d.key);
                else m_userLayer.push_back(std::move(d));
            }

        const wxnyaml::Node schemes = wxnyaml::child(root, "schemes");
        if (wxnyaml::isSeq(schemes))
            for (wxnyaml::Node e : schemes.children())
            {
                std::string id;
                if (!wxnyaml::getText(wxnyaml::child(e, "id"), id) || id.empty()) { m_keptSchemes.push_back(e.id()); continue; }
                KeymapScheme s;
                s.id = fromUtf8(id);
                s.bundled = false;
                s.name = fromUtf8(wxnyaml::textOr(wxnyaml::child(e, "name"), std::string()));
                if (s.name.empty()) s.name = s.id;
                s.parent = fromUtf8(wxnyaml::textOr(wxnyaml::child(e, "parent"), std::string()));
                const wxnyaml::Node kb = wxnyaml::child(e, "bindings");
                if (wxnyaml::isSeq(kb))
                    for (wxnyaml::Node be : kb.children())
                    {
                        KeymapDelta d;
                        if (!deltaFromYaml(be, d)) { m_keptRules.push_back({ s.id, be.id() }); continue; }
                        // editor-tier rules are the scheme's editorDeltas (see KeymapScheme::editorDeltas),
                        // applied by resolveEditor() while the scheme is active
                        (isEditorCommand(d.symbolicName) ? s.editorDeltas : s.deltas).push_back(std::move(d));
                    }
                // refused (false) when the id shadows a bundled preset: the foreign block is ignored
                // rather than corrupting the compiled preset - see registerScheme
                registerScheme(s);
            }
    }

    // The rules of `list` ("" = the top-level bindings, else a user scheme's id) that the file held but this
    // build could not read, back at the end of the list as the file wrote them.
    void keepRules(ryml::Tree& t, wxnyaml::MutNode list, const wxString& listId) const
    {
        for (const auto& kept : m_keptRules)
            if (kept.first == listId) t.duplicate(&m_file.tree, kept.second, list.id(), t.last_child(list.id()));
    }

    // A rule on one line, key first as VS Code writes them. Keys are stored lower-case - how VS Code,
    // Sublime and Pulsar spell them; wx reads any case.
    static void addDelta(wxnyaml::MutNode list, const KeymapDelta& d)
    {
        wxnyaml::MutNode e = wxnyaml::addMapItem(list);
        wxnyaml::setOneLine(e);
        if (!d.key.empty()) wxnyaml::setText(wxnyaml::addKey(e, "key"), std::string(d.key.Lower().utf8_str()));
        wxnyaml::setText(wxnyaml::addKey(e, "command"), (d.unbind ? "-" : "") + std::string(d.symbolicName.utf8_str()));
        if (d.scope != KeyScope::Global) wxnyaml::setText(wxnyaml::addKey(e, "when"), scopeToStr(d.scope));
    }
    std::string serialize() const
    {
        ryml::Tree t;
        wxnyaml::MutNode root = wxnyaml::resetToMap(t);
        wxnyaml::setInteger(wxnyaml::addKey(root, "version"), kCurrentVersion);
        wxnyaml::setText(wxnyaml::addKey(root, "scheme"), std::string(m_activeScheme.utf8_str()));
        wxnyaml::MutNode bindings = wxnyaml::addSeq(root, "bindings");
        for (const KeymapDelta& d : m_userLayer) addDelta(bindings, d);
        // The editor tier's user overrides, in the curated seed order for a stable, diff-friendly file; an
        // empty stored value is an explicit clear ("-<name>", no key). A name not in this build's curated
        // set (a newer build's editor command) is kept, after the known rows, so an older build re-saving
        // cannot silently drop it.
        std::vector<wxString> editorNames;
        for (const EditorRootEntry& r : m_editorRoot)
            if (m_editorUser.find(r.name) != m_editorUser.end()) editorNames.push_back(r.name);
        for (const auto& kv : m_editorUser)
            if (m_editorIndex.find(kv.first) == m_editorIndex.end()) editorNames.push_back(kv.first);
        for (const wxString& name : editorNames)
        {
            KeymapDelta d;
            d.symbolicName = name;
            d.key = m_editorUser.at(name);
            d.unbind = d.key.empty();
            addDelta(bindings, d);
        }
        keepRules(t, bindings, wxString());
        // user (non-bundled) schemes only
        bool anyScheme = !m_keptSchemes.empty();
        for (const KeymapScheme& s : m_schemes) anyScheme = anyScheme || !s.bundled;
        if (anyScheme)
        {
            wxnyaml::MutNode schemes = wxnyaml::addSeq(root, "schemes");
            for (const KeymapScheme& s : m_schemes)
            {
                if (s.bundled) continue;
                wxnyaml::MutNode e = wxnyaml::addMapItem(schemes);
                wxnyaml::setText(wxnyaml::addKey(e, "id"), std::string(s.id.utf8_str()));
                wxnyaml::setText(wxnyaml::addKey(e, "name"), std::string(s.name.utf8_str()));
                if (!s.parent.empty()) wxnyaml::setText(wxnyaml::addKey(e, "parent"), std::string(s.parent.utf8_str()));
                wxnyaml::MutNode sb = wxnyaml::addSeq(e, "bindings");
                for (const KeymapDelta& d : s.deltas) addDelta(sb, d);
                for (const KeymapDelta& d : s.editorDeltas) addDelta(sb, d);   // the scheme's editor tier, round-tripped with it
                keepRules(t, sb, s.id);
            }
            for (ryml::id_type id : m_keptSchemes) t.duplicate(&m_file.tree, id, schemes.id(), t.last_child(schemes.id()));
        }
        for (ryml::id_type id : m_keptTop) t.duplicate(&m_file.tree, id, root.id(), t.last_child(root.id()));
        std::string out;
        if (!wxnyaml::emit(t, out)) return std::string();
        return "# wxNote key bindings: your changes to the built-in keys, applied over the active scheme.\n"
               "# A later rule wins. 'command: -name' removes the key given (or, with no key, every key).\n"
               + out;
    }

    // ---- state ------------------------------------------------------------------------------------
    std::vector<RootEntry>                       m_root;         // Tier 0, menu order
    std::unordered_map<wxString, size_t, WxnKeyHash> m_rootIndex;
    std::vector<KeymapScheme>                    m_schemes;      // bundled (Phase 2) + user
    std::vector<KeymapDelta>                     m_userLayer;    // Tier 3
    wxString                                     m_activeScheme = "wxnote.default";
    wxString                                     m_filePath;
    bool                                         m_readOnly = false;
    wxString                                     m_loadError;    // see loadError()
    // What the file holds that this build cannot read, written back on save so a typo or a newer build's
    // field survives (docs/SETTINGS_DESIGN.md, "Unknown keys are kept"): node ids in m_file, the parsed
    // file, whose arena the duplicated entries point into - so it lives as long as they do.
    wxnyaml::Doc                                 m_file;
    std::vector<ryml::id_type>                   m_keptTop;      // top-level entries it does not know
    std::vector<std::pair<wxString, ryml::id_type>> m_keptRules; // (list: "" or a scheme id, rule)
    std::vector<ryml::id_type>                   m_keptSchemes;  // `schemes` entries with no id

    std::unordered_map<wxString, EffectiveBinding, WxnKeyHash> m_eff;
    std::vector<wxString>                        m_effOrder;     // stable menu order for all()
    std::unordered_map<int, wxString>            m_cmdToSym;

    // editor tier: Tier 0 curated defaults + a name->key user layer (empty value == cleared),
    // resolved into m_editorEff. Kept wholly separate from the menu-command state above.
    std::vector<EditorRootEntry>                 m_editorRoot;
    std::unordered_map<wxString, size_t, WxnKeyHash> m_editorIndex;
    std::unordered_map<wxString, wxString, WxnKeyHash> m_editorUser;
    std::vector<EditorEffective>                 m_editorEff;
    std::unordered_map<wxString, size_t, WxnKeyHash> m_editorEffIndex;
};
