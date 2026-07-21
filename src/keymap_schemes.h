#pragma once
// =====================================================================
// keymap_schemes.h - the compiled-in, read-only BUNDLED keymap presets (Tier 1 of the shortcut model).
// These are locale-independent C DATA, never mutated at
// runtime: a scheme is a NAMED, switchable set of divergences from wxNote's own Tier-0 defaults, and a
// bundled preset is one the app ships and the user can never overwrite in place (edits copy-on-write into
// a user scheme, or land in the user layer - KeymapStore::duplicateScheme / rebind).
//
// Exactly ONE preset ships: "wxNote" (id "wxnote.default") - the IDENTITY preset: no deltas, it
// simply IS wxNote's own Tier-0 defaults. It exists so a scheme picker can list/select "the built-in
// keys" by name; KeymapStore::activeSchemeChain() treats "wxnote.default" as the chain root and never
// walks its (empty) delta list, so it is a pure label with zero resolution cost.
//
// There is deliberately NO compiled-in "Notepad++" preset any more. wxNote inherited Notepad++'s Windows
// defaults wholesale (kCmd* ids ARE the frozen IDM_* values, src/command_ids.h), so the two keymaps
// already agree on the headline bindings - and the remaining divergences could only be guessed at from
// out-of-tree N++ sources. Instead of shipping a guess, Notepad++ keys arrive through the optional
// npp-shortcuts-compat plugin, which imports the user's ACTUAL shortcuts.xml as a "Notepad++ (imported)"
// user scheme via nib.keymap/1 - authoritative, per-user, and persisted in shortcuts.json. The
// registration machinery below (registerBundledScheme + the delta-table shape) stays: user/plugin
// schemes resolve through the same store paths, and tests drive it with synthetic tables.
//
// Registration ordering matters - see registerKeymapSchemes() below: call it AFTER seedKeymapDefaults()
// and BEFORE KeymapStore::load(), so the file's activeScheme can resolve at startup and load()'s reload
// cleanup (which keeps bundled schemes, drops user ones) preserves the preset. A shortcuts.json whose
// activeScheme names a scheme that no longer exists (e.g. the removed "notepad++" preset) snaps back to
// "wxnote.default" inside load() - the file migrates itself on the next save.
// =====================================================================
#include "keymap_store.h"

// A single divergence of a bundled preset from wxNote's own Tier-0 defaults.
//   accelRaw == nullptr  => UNBIND: suppress the inherited default (the command becomes unbound here).
//   accelRaw != nullptr  => BIND:   the canonical accelerator spelling to assign ("Ctrl+K").
// (An inline constexpr variable so the table is one shared definition across every translation unit that
// includes this header - avoids the internal-linkage-static-referenced-by-an-inline-function ODR hazard.)
struct SchemeDelta
{
    const char* symbolicName;
    const char* accelRaw;
};

// Build a KeymapScheme (bundled=true, read-only) from a static SchemeDelta table and register it.
// nullptr accelRaw becomes a full unbind (KeymapDelta.unbind, empty key); a spelling becomes a plain
// Global bind. The store canonicalizes/normalizes the spelling itself at resolve time.
inline void registerBundledScheme(KeymapStore& store, const wxString& id, const wxString& name,
                                  const wxString& parent, const SchemeDelta* rows, size_t count)
{
    KeymapScheme s;
    s.id      = id;
    s.name    = name;
    s.parent  = parent;
    s.bundled = true;
    for (size_t i = 0; i < count; ++i)
    {
        KeymapDelta d;
        d.symbolicName = wxString::FromAscii(rows[i].symbolicName);
        d.scope        = KeyScope::Global;
        if (rows[i].accelRaw && *rows[i].accelRaw) { d.unbind = false; d.key = wxString::FromAscii(rows[i].accelRaw); }
        else                                       { d.unbind = true;  d.key = wxString(); }
        s.deltas.push_back(std::move(d));
    }
    store.registerScheme(s);
}

// Register every bundled read-only preset into the store. Call ONCE at startup, after seedKeymapDefaults()
// (so Tier 0 exists) and before KeymapStore::load() (so an activeScheme pointer in shortcuts.json resolves,
// and so load()'s "keep bundled, drop user" cleanup preserves these on a reload).
inline void registerKeymapSchemes(KeymapStore& store)
{
    registerBundledScheme(store, "wxnote.default", "wxNote", wxString(), nullptr, 0);
}
