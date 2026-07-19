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
//   Tier 3  user layer       - userKeybindings from shortcuts.json, applied last, highest precedence,
//                              surviving scheme switches (VS Code semantics).
//
// The result is keyed by each command's stable symbolicName (src/menu_model.h). It is applied to the
// frame's one wxAcceleratorTable and to the menu labels by WxnShellFrame::refreshAccelerators().
//
// Phase 1 scope: model + load/resolve/save + hand-editable shortcuts.json. No UI (the mapper dialog is
// Phase 3); no bundled schemes yet (Phase 2). save() is implemented but NOT called at runtime in Phase 1
// - the file is load-only / hand-edited, per the "written immediately on mapper OK, never on
// exit" rule.
// =====================================================================
#include <wx/string.h>
#include <wx/accel.h>
#include <wx/file.h>
#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/log.h>
#include <wx/stdpaths.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <functional>
#include <cstdio>
#include <cctype>
#include <cstdlib>

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
                                        // ToRawString() normalization of the shortcuts.json key.
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

// A single override row (a userKeybindings entry, or a scheme delta). A leading '-' on the command marks
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
    // npp-shortcuts-compat import - stores its ScintillaKeys here so a later activation applies them).
    // Appended LAST so any positional KeymapScheme initializer that stops earlier stays valid.
    std::vector<KeymapDelta> editorDeltas;
};

// ----- the Scintilla editor tier (Phase 4) -------------------------------------------------
// Editor commands are a SEPARATE space from menu commands: they are keyed by a stable ascii name
// ("editor.lineCut"), carry a Scintilla SCI_* id instead of a menu command id, live only in KeyScope
// Editor, and persist under shortcuts.json's own "editor" section (never mixed into userKeybindings). The
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
    // hand-edited shortcuts.json via parseInto, and a plugin via the nib.keymap commit) are covered.
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
    // startup - mirrors the contextMenu.xml load pattern). Populates the active-scheme pointer, the user
    // layer, and any user schemes, then resolves. Does NOT write anything (load-only in Phase 1).
    void load(const wxString& userDataDir)
    {
        m_filePath = userDataDir + wxFILE_SEP_PATH + "shortcuts.json";
        m_userLayer.clear();
        m_editorUser.clear();
        // drop previously-loaded NON-bundled schemes so a reload is clean; keep bundled presets
        m_schemes.erase(std::remove_if(m_schemes.begin(), m_schemes.end(),
                                       [](const KeymapScheme& s){ return !s.bundled; }), m_schemes.end());
        m_activeScheme = "wxnote.default";
        m_readOnly = false;

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
        // in the file itself, e.g. the removed bundled "notepad++" preset in a pre-existing
        // shortcuts.json - must not stick: resolution would already fall back to the default chain
        // (activeSchemeChain() stops at an unknown id), but the id would be re-serialized by every
        // save() and dangle forever, and the mapper's picker could never show the real selection. Snap
        // it back to the root HERE, after parseInto has registered the file's own schemes, so an id
        // defined later in the same file still resolves and only a truly unknown one migrates.
        if (m_activeScheme != "wxnote.default" && !schemeById(m_activeScheme))
            m_activeScheme = "wxnote.default";
        resolveAll();   // also rebuilds the editor tier (resolveAll ends in resolveEditor)
    }

    // Write shortcuts.json (immediately on a mapper OK in later phases - never on exit). Refuses to write
    // a file a newer wxNote wrote (version > current) so unknown fields aren't clobbered.
    bool save() const
    {
        if (m_readOnly || m_filePath.empty()) return false;
        wxLogNull noLog;
        wxFileName::Mkdir(wxFileName(m_filePath).GetPath(), wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
        std::string out = serialize();
        // Atomic replace: fully write a sibling temp, flush it, then rename over the target. A plain
        // wxFile::write on m_filePath O_TRUNCs the existing shortcuts.json BEFORE writing, so a crash /
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
    bool activeSchemeIsBundled() const { return schemeIsBundled(m_activeScheme); }

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

    // ================= minimal JSON =================
    // No JSON library is vendored; the schema is tiny, so a small recursive-descent reader +
    // a hand writer keep this header self-contained. Untrusted input: every failure degrades to "ignore
    // and keep resolving", never a throw/crash (a bad hand-edit must not brick startup).
    struct Json
    {
        enum Type { Null, Bool, Num, Str, Arr, Obj } type = Null;
        bool b = false; double num = 0; std::string str;
        std::vector<Json> arr;
        std::vector<std::pair<std::string, Json>> obj;   // insertion-ordered
        const Json* member(const char* k) const
        {
            if (type != Obj) return nullptr;
            for (const auto& kv : obj) if (kv.first == k) return &kv.second;
            return nullptr;
        }
        wxString wxstr() const { return type == Str ? wxString::FromUTF8(str.c_str()) : wxString(); }
    };

    struct JsonParser
    {
        const std::string& s; size_t i = 0; bool ok = true;
        // Nesting bound: object()/array() recurse back through value(), so without a cap a file of a
        // few thousand nested brackets overflows the stack - an UNCATCHABLE crash that would break the
        // "a bad hand-edit must not brick startup" contract above (the parse must FAIL, load must fall
        // back to defaults). Siblings share a level (the counter tracks true nesting only), so wide-
        // but-shallow input is unaffected; the real schema nests ~4 levels, 64 is generous. Mirrors
        // the activeSchemeChain() parent-cycle guard.
        static constexpr int kMaxDepth = 64;
        int depth = 0;
        explicit JsonParser(const std::string& src) : s(src) {}
        void ws() { while (i < s.size() && (s[i]==' '||s[i]=='\t'||s[i]=='\n'||s[i]=='\r')) ++i; }
        bool parse(Json& out) { ws(); bool r = value(out); ws(); return r && ok; }
        bool value(Json& v)
        {
            ws();
            if (i >= s.size()) return fail();
            if (depth >= kMaxDepth) return fail();   // too deeply nested: give up cleanly (see kMaxDepth)
            ++depth;
            const bool r = valueDispatch(v);
            --depth;
            return r;
        }
        bool valueDispatch(Json& v)
        {
            char c = s[i];
            if (c == '{') return object(v);
            if (c == '[') return array(v);
            if (c == '"') { v.type = Json::Str; return str(v.str); }
            if (c == 't' || c == 'f') return boolean(v);
            if (c == 'n') { if (s.compare(i,4,"null")==0){ i+=4; v.type=Json::Null; return true;} return fail(); }
            return number(v);
        }
        bool object(Json& v)
        {
            v.type = Json::Obj; ++i; ws();
            if (i < s.size() && s[i]=='}') { ++i; return true; }
            for (;;)
            {
                ws(); if (i>=s.size()||s[i]!='"') return fail();
                std::string key; if (!str(key)) return false;
                ws(); if (i>=s.size()||s[i]!=':') return fail(); ++i;
                Json child; if (!value(child)) return false;
                v.obj.emplace_back(std::move(key), std::move(child));
                ws(); if (i>=s.size()) return fail();
                if (s[i]==',') { ++i; continue; }
                if (s[i]=='}') { ++i; return true; }
                return fail();
            }
        }
        bool array(Json& v)
        {
            v.type = Json::Arr; ++i; ws();
            if (i < s.size() && s[i]==']') { ++i; return true; }
            for (;;)
            {
                Json child; if (!value(child)) return false;
                v.arr.push_back(std::move(child));
                ws(); if (i>=s.size()) return fail();
                if (s[i]==',') { ++i; continue; }
                if (s[i]==']') { ++i; return true; }
                return fail();
            }
        }
        bool str(std::string& out)
        {
            if (i>=s.size()||s[i]!='"') return fail(); ++i;
            while (i < s.size())
            {
                char c = s[i++];
                if (c=='"') return true;
                if (c=='\\')
                {
                    if (i>=s.size()) return fail();
                    char e = s[i++];
                    switch (e)
                    {
                        case '"': out+='"'; break;   case '\\': out+='\\'; break; case '/': out+='/'; break;
                        case 'b': out+='\b'; break;  case 'f': out+='\f'; break;  case 'n': out+='\n'; break;
                        case 'r': out+='\r'; break;  case 't': out+='\t'; break;
                        case 'u':
                        {
                            if (i+4 > s.size()) return fail();
                            unsigned cp = 0;
                            for (int k=0;k<4;++k){ char h=s[i++]; cp<<=4;
                                if(h>='0'&&h<='9')cp|=h-'0'; else if(h>='a'&&h<='f')cp|=h-'a'+10;
                                else if(h>='A'&&h<='F')cp|=h-'A'+10; else return fail(); }
                            appendUtf8(out, cp);
                            break;
                        }
                        default: return fail();
                    }
                }
                else out += c;
            }
            return fail();
        }
        bool boolean(Json& v)
        {
            if (s.compare(i,4,"true")==0){ i+=4; v.type=Json::Bool; v.b=true; return true; }
            if (s.compare(i,5,"false")==0){ i+=5; v.type=Json::Bool; v.b=false; return true; }
            return fail();
        }
        bool number(Json& v)
        {
            size_t start = i;
            while (i<s.size() && (isdigit((unsigned char)s[i])||s[i]=='-'||s[i]=='+'||s[i]=='.'||s[i]=='e'||s[i]=='E')) ++i;
            if (i==start) return fail();
            v.type = Json::Num; v.num = atof(s.substr(start, i-start).c_str());
            return true;
        }
        static void appendUtf8(std::string& out, unsigned cp)
        {
            if (cp < 0x80) out += (char)cp;
            else if (cp < 0x800) { out += (char)(0xC0|(cp>>6)); out += (char)(0x80|(cp&0x3F)); }
            else { out += (char)(0xE0|(cp>>12)); out += (char)(0x80|((cp>>6)&0x3F)); out += (char)(0x80|(cp&0x3F)); }
        }
        bool fail() { ok = false; return false; }
    };

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

    // Parse one binding-entry object into a KeymapDelta. A leading '-' on "command" is an unbind.
    static bool deltaFromJson(const Json& e, KeymapDelta& out)
    {
        if (e.type != Json::Obj) return false;
        const Json* cmd = e.member("command");
        if (!cmd || cmd->type != Json::Str || cmd->str.empty()) return false;
        wxString command = cmd->wxstr();
        out.unbind = command.StartsWith("-");
        if (out.unbind) command = command.Mid(1);
        if (command.empty()) return false;
        out.symbolicName = command;
        const Json* key = e.member("key");
        out.key = (key && key->type == Json::Str) ? key->wxstr() : wxString();
        const Json* when = e.member("when");
        out.scope = when && when->type == Json::Str ? scopeFromStr(when->wxstr()) : KeyScope::Global;
        return !(out.unbind == false && out.key.empty());   // a non-unbind must carry a key
    }

    void parseInto(const std::string& text)
    {
        JsonParser p(text);
        Json root;
        if (!p.parse(root) || root.type != Json::Obj) return;   // unparsable => defaults only

        if (const Json* v = root.member("version"))
            if (v->type == Json::Num && (int)v->num > kCurrentVersion) m_readOnly = true;  // newer file: don't clobber

        if (const Json* a = root.member("activeScheme"))
            if (a->type == Json::Str && !a->str.empty()) m_activeScheme = a->wxstr();

        if (const Json* uk = root.member("userKeybindings"))
            if (uk->type == Json::Arr)
                for (const Json& e : uk->arr)
                { KeymapDelta d; if (deltaFromJson(e, d)) m_userLayer.push_back(std::move(d)); }

        if (const Json* sc = root.member("schemes"))
            if (sc->type == Json::Arr)
                for (const Json& e : sc->arr)
                {
                    if (e.type != Json::Obj) continue;
                    const Json* id = e.member("id");
                    if (!id || id->type != Json::Str || id->str.empty()) continue;
                    KeymapScheme s; s.id = id->wxstr(); s.bundled = false;
                    if (const Json* nm = e.member("name"))   if (nm->type == Json::Str) s.name = nm->wxstr();
                    if (s.name.empty()) s.name = s.id;
                    if (const Json* pr = e.member("parent")) if (pr->type == Json::Str) s.parent = pr->wxstr();
                    if (const Json* kb = e.member("keybindings"))
                        if (kb->type == Json::Arr)
                            for (const Json& be : kb->arr)
                            { KeymapDelta d; if (deltaFromJson(be, d)) s.deltas.push_back(std::move(d)); }
                    // the scheme's EDITOR-tier deltas (same entry shape, "editor.*" names) - see
                    // KeymapScheme::editorDeltas; applied by resolveEditor() when the scheme is active
                    if (const Json* ed = e.member("editor"))
                        if (ed->type == Json::Arr)
                            for (const Json& be : ed->arr)
                            { KeymapDelta d; if (deltaFromJson(be, d)) s.editorDeltas.push_back(std::move(d)); }
                    // refused (false) when the id shadows a bundled preset: the foreign block is
                    // ignored rather than corrupting the compiled preset - see registerScheme
                    registerScheme(s);
                }

        // The editor tier: { "command": "editor.lineCut", "key": "ctrl+k" } rebinds; a leading
        // '-' ("-editor.lineCut", key optional) is an explicit CLEAR. Kept in its own map, applied to the
        // curated editor roots by resolveEditor(); a name this build doesn't define is retained but ignored.
        if (const Json* ed = root.member("editor"))
            if (ed->type == Json::Arr)
                for (const Json& e : ed->arr)
                {
                    KeymapDelta d;
                    if (!deltaFromJson(e, d)) continue;
                    m_editorUser[d.symbolicName] = d.unbind ? wxString() : keySpell::canonical(d.key);
                }
    }

    static void jsonEscape(std::string& out, const wxString& w)
    {
        const std::string s(w.utf8_str());
        for (char c : s)
        {
            switch (c)
            {
                case '"':  out += "\\\""; break; case '\\': out += "\\\\"; break;
                case '\n': out += "\\n";  break; case '\r': out += "\\r";  break; case '\t': out += "\\t"; break;
                default:
                    if ((unsigned char)c < 0x20) { char buf[8]; snprintf(buf, sizeof buf, "\\u%04x", c); out += buf; }
                    else out += c;
            }
        }
    }
    static void writeDelta(std::string& out, const KeymapDelta& d, const char* indent)
    {
        out += indent; out += "{ \"command\": \"";
        if (d.unbind) out += "-";
        jsonEscape(out, d.symbolicName); out += "\"";
        if (!d.key.empty()) { out += ", \"key\": \""; jsonEscape(out, d.key); out += "\""; }
        if (d.scope != KeyScope::Global) { out += ", \"when\": \""; out += scopeToStr(d.scope); out += "\""; }
        out += " }";
    }
    std::string serialize() const
    {
        std::string out;
        out += "{\n  \"version\": " + std::to_string(kCurrentVersion) + ",\n";
        out += "  \"activeScheme\": \""; jsonEscape(out, m_activeScheme); out += "\",\n";
        out += "  \"userKeybindings\": [";
        for (size_t k = 0; k < m_userLayer.size(); ++k)
        { out += (k ? ",\n" : "\n"); writeDelta(out, m_userLayer[k], "    "); }
        out += m_userLayer.empty() ? "]" : "\n  ]";
        // user (non-bundled) schemes only
        std::vector<const KeymapScheme*> userSchemes;
        for (const KeymapScheme& s : m_schemes) if (!s.bundled) userSchemes.push_back(&s);
        if (!userSchemes.empty())
        {
            out += ",\n  \"schemes\": [\n";
            for (size_t si = 0; si < userSchemes.size(); ++si)
            {
                const KeymapScheme& s = *userSchemes[si];
                out += "    { \"id\": \""; jsonEscape(out, s.id); out += "\", \"name\": \""; jsonEscape(out, s.name); out += "\"";
                if (!s.parent.empty()) { out += ", \"parent\": \""; jsonEscape(out, s.parent); out += "\""; }
                out += ", \"keybindings\": [";
                for (size_t k = 0; k < s.deltas.size(); ++k)
                { out += (k ? ",\n" : "\n"); writeDelta(out, s.deltas[k], "      "); }
                out += s.deltas.empty() ? "]" : "\n    ]";
                if (!s.editorDeltas.empty())    // the scheme's editor-tier deltas, round-tripped with it
                {
                    out += ", \"editor\": [";
                    for (size_t k = 0; k < s.editorDeltas.size(); ++k)
                    { out += (k ? ",\n" : "\n"); writeDelta(out, s.editorDeltas[k], "      "); }
                    out += "\n    ]";
                }
                out += " }";
                out += (si + 1 < userSchemes.size()) ? ",\n" : "\n";
            }
            out += "  ]";
        }
        // the editor tier's user overrides. Emitted in the curated seed order for a stable,
        // diff-friendly file; an empty stored value is an explicit clear -> "-<name>" (no key). Any name
        // not in this build's curated set (a newer build's editor command) is retained verbatim after the
        // known rows so an older build re-saving can't silently drop it (forward-compat).
        std::vector<wxString> editorNames;
        for (const EditorRootEntry& r : m_editorRoot)
            if (m_editorUser.find(r.name) != m_editorUser.end()) editorNames.push_back(r.name);
        for (const auto& kv : m_editorUser)
            if (m_editorIndex.find(kv.first) == m_editorIndex.end()) editorNames.push_back(kv.first);
        if (!editorNames.empty())
        {
            out += ",\n  \"editor\": [";
            for (size_t k = 0; k < editorNames.size(); ++k)
            {
                const wxString& name = editorNames[k];
                const wxString& key  = m_editorUser.at(name);
                KeymapDelta d;
                d.symbolicName = name;
                d.key          = key;
                d.unbind       = key.empty();     // stored empty => an explicit clear
                out += (k ? ",\n" : "\n");
                writeDelta(out, d, "    ");
            }
            out += "\n  ]";
        }
        out += "\n}\n";
        return out;
    }

    // ---- state ------------------------------------------------------------------------------------
    std::vector<RootEntry>                       m_root;         // Tier 0, menu order
    std::unordered_map<wxString, size_t, WxnKeyHash> m_rootIndex;
    std::vector<KeymapScheme>                    m_schemes;      // bundled (Phase 2) + user
    std::vector<KeymapDelta>                     m_userLayer;    // Tier 3
    wxString                                     m_activeScheme = "wxnote.default";
    wxString                                     m_filePath;
    bool                                         m_readOnly = false;

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
