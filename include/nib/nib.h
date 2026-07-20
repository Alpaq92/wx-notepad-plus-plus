// SPDX-License-Identifier: Apache-2.0
//
// Nib - the wxNote plugin API (codename "Nib", as in a pen nib).
// Copyright 2026 The wxNote Authors.
//
// An original, cross-platform, stable C ABI for extending wxNote. It is clean-sheet work: it
// borrows nothing from Notepad++ (no NPPM_* numbers, no FuncItem/NppData/SCNotification, no WM_USER,
// and no platform handles such as HWND in the contract). A plugin is a shared library exporting a
// single entry point; it asks the host for capability interfaces by stable id + version and gets back
// typed function tables. This file (and the API it defines) is Apache-2.0, matching the rest of the
// core (see LICENSING.md) - only the optional npp-bridge module stays GPL.
//
// This is the Part-1 surface: enough to load a plugin, register a command, and edit text. It will grow
// (documents, panels, events, language services, ...) additively - every struct is length-prefixed and
// every interface is independently versioned, so new fields/interfaces never break old plugins.

#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NIB_ABI_VERSION 0x00010005u   // (major << 16) | minor  ->  1.5 (additive: nib.events v4 long-tail file lifecycle + command-line plugin message)

#if defined(_WIN32)
  #define NIB_API __declspec(dllexport)
#elif defined(__GNUC__)
  #define NIB_API __attribute__((visibility("default")))
#else
  #define NIB_API
#endif

typedef struct NibHost NibHost;   // opaque host handle (never dereferenced by plugins)

// Ask the host for a capability interface (a const function table) by stable id + minimum version.
// Returns NULL if the host can't satisfy it. This single call is both capability negotiation and
// versioning - unknown ids simply return NULL, so there is no global number space to collide on.
typedef const void* (*NibQueryFn)(NibHost* host, const char* iface_id, uint32_t min_version);
typedef void        (*NibLogFn)(NibHost* host, int level, const char* msg);

// What the host hands a plugin at load time.
typedef struct NibBootstrap {
    uint32_t   abi_version;   // NIB_ABI_VERSION the host speaks
    uint32_t   struct_size;   // sizeof(NibBootstrap) - lets this struct grow compatibly
    NibHost*   host;
    NibQueryFn query;
    NibLogFn   log;
} NibBootstrap;

// ---- nib.host/1 : host identity ------------------------------------------------------------------
#define NIB_IFACE_HOST "nib.host/1"
typedef struct NibHostApi {
    uint32_t    version;
    uint32_t    struct_size;
    const char* (*product_name)(NibHost*);   // e.g. "wxNote"
    uint32_t    (*abi_version)(NibHost*);     // the host's NIB_ABI_VERSION
} NibHostApi;

// ---- nib.editor/1 : the active editor ------------------------------------------------------------
#define NIB_IFACE_EDITOR "nib.editor/1"
typedef struct NibEditorApi {
    uint32_t version;
    uint32_t struct_size;
    int64_t  (*length)(NibHost*);                                       // bytes (UTF-8)
    void     (*insert_text)(NibHost*, int64_t pos, const char* utf8);   // pos < 0 => at the caret
    void     (*replace_selection)(NibHost*, const char* utf8);
    int64_t  (*selection_start)(NibHost*);
    int64_t  (*selection_end)(NibHost*);
    // Copy [start,end) into buf (NUL-terminated if it fits); returns the byte length of the range.
    int64_t  (*get_text)(NibHost*, int64_t start, int64_t end, char* buf, int64_t buf_size);
} NibEditorApi;

// ---- nib.documents/1 : the open documents --------------------------------------------------------
#define NIB_IFACE_DOCUMENTS "nib.documents/1"
typedef struct NibDocumentsApi {
    uint32_t version;
    uint32_t struct_size;
    int  (*count)(NibHost*);                        // number of open documents
    // Copy the active document's full path (UTF-8) into buf (NUL-terminated if it fits); returns the
    // byte length excluding the NUL, or 0 if the document is untitled (no path on disk yet).
    int  (*active_path)(NibHost*, char* buf, int cap);
    int  (*open)(NibHost*, const char* utf8_path);  // open a file (load it into a tab); 1 on success, 0 on failure
    int  (*save_active)(NibHost*);                  // save the active document to disk; 1 on success
    // ---- v2 additions (struct grows only at the end; guard with `version >= 2` before calling) -----
    // A stable, opaque per-document id (a Notepad++ "buffer id"): unique per open document, valid until
    // that document closes. 0 when there is no active document.
    intptr_t (*active_id)(NibHost*);
    // Copy the UTF-8 path of the document whose id is `id` into buf (NUL-terminated if it fits); returns
    // the byte length excluding the NUL, or 0 if no open document has that id (or it is untitled).
    int  (*path_from_id)(NibHost*, intptr_t id, char* buf, int cap);
    // ---- v3 ---- which editor view holds the active document: 0 = main, 1 = sub. Lets NPPM_GETCURRENTVIEW
    // / NPPM_GETCURRENTSCINTILLA report the focused pane so view-aware plugins target the right editor.
    int  (*active_view)(NibHost*);
    // ---- v4 ---- copy the UTF-8 path of the open document at flat `index` (0-based, across every view/tab,
    // matching count()) into buf; returns the byte length (0 if untitled or index out of range). Pairs with
    // count() to enumerate every open file (e.g. NPPM_GETOPENFILENAMES).
    int  (*doc_path_at)(NibHost*, int index, char* buf, int cap);
    // ---- v5 (guard with `version >= 5`) : per-view buffer model + per-buffer properties -------------
    // These walk the open documents PER VIEW (0 = main, 1 = sub), address a document by its (view, index)
    // position or by buffer id in either direction, and read/modify per-buffer properties (language,
    // on-disk encoding, EOL line-ending mode, tab colour) without disturbing the active view. A buffer id
    // is the v2 `active_id`/`path_from_id` id. Background (non-active) buffers are inspected/written via a
    // doc-pointer-swap peek that saves and restores the active view's caret/scroll.
    int      (*view_count)(NibHost*, int view);                 // open documents in `view`; 0 if that view has none
    intptr_t (*id_at)(NibHost*, int view, int index);           // buffer id at 0-based (view,index); 0 if out of range
    // Locate buffer id `id`: writes its view (0/1) to *out_view and 0-based in-view index to *out_index and
    // returns 1; returns 0 if no open document has that id. `priority_view` (0/1) is searched first.
    int      (*pos_of)(NibHost*, intptr_t id, int priority_view, int* out_view, int* out_index);
    int      (*index_of_active)(NibHost*, int view);            // 0-based index of `view`'s active doc; -1 if the view is empty
    int      (*activate_at)(NibHost*, int view, int index);     // select+focus the doc at (view,index); 1 on success, 0 if out of range
    // Force a host Language on the document with buffer id `id`. `lang_cmd_id` is a host language-menu
    // command id (the frozen kCmd*/IDM_LANG_* number space). 1 on success, 0 if the id or command is unknown.
    int      (*set_lang_by_id)(NibHost*, intptr_t id, int lang_cmd_id);
    // On-disk ENCODING of buffer id `id`, as the host's own encoding enum:
    //   0 = UTF-8 (no BOM), 1 = UTF-8 BOM, 2 = UTF-16 LE, 3 = UTF-16 BE, 4 = ANSI/system, 5 = codepage.
    // get returns the enum, or -1 if no such id. set records it and returns 1 (0 if no such id, or the enum
    // is out of the settable range 0..4); the change is applied on the next SAVE - the host is save-to-apply,
    // unlike N++'s immediate re-decode. Encoding 5 (codepage) can be reported but not set blind (no codepage).
    int      (*encoding_get_by_id)(NibHost*, intptr_t id);
    int      (*encoding_set_by_id)(NibHost*, intptr_t id, int encoding);
    // EOL line-ending mode of buffer id `id`, as a Scintilla SC_EOL_* value (0=CRLF, 1=CR, 2=LF). get
    // returns the mode or -1 if no such id; set converts the buffer's existing line endings to `eol_mode`
    // and marks it dirty, returning 1 (0 if no such id or `eol_mode` is out of 0..2).
    int      (*eol_get_by_id)(NibHost*, intptr_t id);
    int      (*eol_set_by_id)(NibHost*, intptr_t id, int eol_mode);
    // Save the ACTIVE document to `utf8_path`. as_copy != 0 writes a copy WITHOUT rebinding the active
    // document to that path (its own path/title/dirty untouched); as_copy == 0 is Save-As (the active
    // document becomes `utf8_path`). 1 on success, 0 on write failure (or no active document).
    int      (*save_active_as)(NibHost*, const char* utf8_path, int as_copy);
    // Save the (already-open) document with buffer id `id` to its own on-disk path, WITHOUT activating it
    // (a background buffer is written via the doc-swap peek). 1 on success; 0 if no such id, it is untitled,
    // or the write failed.
    int      (*save_by_id)(NibHost*, intptr_t id);
    int      (*set_dirty_active)(NibHost*);                     // mark the active document dirty; 1 on success, 0 if none
    // Rename an UNTITLED document (buffer id `id`) to `utf8_name` (a tab label, not a path). Returns 0 if
    // the document already has an on-disk path, `utf8_name` is empty, or `utf8_name` contains a path
    // separator; 1 on success.
    int      (*rename_untitled)(NibHost*, intptr_t id, const char* utf8_name);
    // Tab-colour id of the document at (view, index): the host's 0-based palette slot 0..4, or -1 if the
    // tab has no colour (or its colour is not a palette entry).
    int      (*tab_color_id)(NibHost*, int view, int index);
} NibDocumentsApi;

// ---- nib.commands/1 : register + run commands ----------------------------------------------------
#define NIB_IFACE_COMMANDS "nib.commands/1"
// A command handler receives the host + query (to reach other interfaces) and its own user pointer.
typedef void (*NibCommandFn)(NibHost* host, NibQueryFn query, void* user);
typedef struct NibCommandsApi {
    uint32_t version;
    uint32_t struct_size;
    // Register a command; the host surfaces it (e.g. in the Plugins menu) and calls fn when invoked.
    void (*register_command)(NibHost*, const char* id, const char* title, NibCommandFn fn, void* user);
    // v2: invoke one of the host's OWN commands by its numeric menu/command id. Lets a plugin trigger a
    // built-in action (e.g. the N++ bridge serving NPPM_MENUCOMMAND) portably, with no window message.
    void (*invoke_command)(NibHost*, int id);
} NibCommandsApi;

// ---- nib.events/1 : subscribe to editor / document events ----------------------------------------
// The interface is version-gated at query time (the struct layout is identical - only semantics differ):
//   * query(..., NIB_IFACE_EVENTS, 1) -> the v1 table. NIB_EV_DOCUMENT_SAVED keeps its original,
//     savepoint-derived meaning: it fires whenever the editor reaches a save point, which INCLUDES
//     undo/redo landing back on the last saved state - and carries no payload. Existing v1 plugins
//     keep exactly the behaviour they were built against.
//   * query(..., NIB_IFACE_EVENTS, 2) -> the v2 table. NIB_EV_DOCUMENT_SAVED fires exactly once per
//     REAL disk write (never on undo-to-savepoint; a background Save All write reports the id of the
//     buffer actually written, not the active one) and carries as.document.id. v2 also adds
//     NIB_EV_DOCUMENT_SAVING and NIB_EV_DOCUMENT_BEFORE_OPEN below.
//   * query(..., NIB_IFACE_EVENTS, 3) -> the v3 table. Same DOCUMENT_SAVED semantics as v2, plus the
//     new v3 event kinds (NIB_EV_TEXT_MODIFIED and the language/style/shortcut/shutdown notifications
//     below) and the set_modified_mask entry that arms NIB_EV_TEXT_MODIFIED. A plugin that wants the
//     raw-modification stream or the shutdown/language events subscribes through this table.
//   * query(..., NIB_IFACE_EVENTS, 4) -> the v4 table (identical layout to v3; only version differs).
//     It adds the long-tail file-lifecycle kinds (rename / delete / read-only / tab-order /
//     recovery-restore / before-load) and NIB_EV_CMDLINE_PLUGIN_MSG. A plugin that wants any of those
//     subscribes through this table; an older host returns NULL for min_version 4, so a consumer can
//     tell whether the host emits them.
#define NIB_IFACE_EVENTS "nib.events/1"
typedef enum {
    NIB_EV_TEXT_CHANGED = 1,    // as.text:      pos, added, removed (bytes)
    NIB_EV_SELECTION_CHANGED,   // as.selection: anchor, caret
    NIB_EV_DOCUMENT_SAVED,      // v1: no payload, savepoint-derived. v2: as.document.id, one per real disk write.
    NIB_EV_DOCUMENT_ACTIVATED,  // as.document:   id (the now-active document's buffer id)
    NIB_EV_DOCUMENT_OPENED,     // as.document:   id (a document just opened/loaded)
    NIB_EV_DOCUMENT_CLOSED,     // as.document:   id (a document about to close). Ordering guarantee: this
                                //   fires BEFORE the document is torn down on every close path, so the id -
                                //   and its path via nib.documents path_from_id - stay resolvable for the
                                //   whole callback (a bridge can derive its own "before close" from it).
    // ---- v2 additions (subscribe only through the v2 table) --------------------------------------
    NIB_EV_DOCUMENT_SAVING = 7, // as.document: id - about to write this buffer to disk (fires before the
                                //   write is attempted, whether or not it then succeeds; during Save All it
                                //   fires once per buffer, each with that buffer's own id)
    NIB_EV_DOCUMENT_BEFORE_OPEN = 8, // as.document: id - a file document was created but its content has
                                //   not been loaded yet (fires before the first byte is read; the id and
                                //   its path are already resolvable). Real files only, like DOCUMENT_OPENED.
    // ---- v3 additions (subscribe through the v3 table; new kinds, purely additive) ---------------
    NIB_EV_TEXT_MODIFIED = 9,   // as.modified: the RAW Scintilla modification - the full SC_MOD_*/
                                //   SC_PERFORMED_* modification_type plus pos/length/lines_added, unlike
                                //   the insert/delete-only NIB_EV_TEXT_CHANGED. The host fires it ONLY when
                                //   the modification's flags intersect the mask set via set_modified_mask;
                                //   with the default mask 0 it is NEVER fired, so a subscriber pays nothing
                                //   until it arms at least one flag. This gating is host-side and per
                                //   modification (perf-critical: SCN_MODIFIED is one of the highest-volume
                                //   editor signals).
    NIB_EV_LANGUAGE_CHANGED = 10, // as.document: id - the (re)assignment of a buffer's Language (a manual
                                //   Language-menu pick or a set_lang_by_id call); not fired on the automatic
                                //   extension-based re-lex that open/activate/theme changes trigger.
    NIB_EV_STYLE_UPDATED = 11,  // as.document: id (the active buffer) - the editor theme / token styles were
                                //   re-applied (e.g. the Style Configurator committed a change). "Restyle
                                //   your own surfaces"; carries no per-buffer detail beyond the active id.
    NIB_EV_SHORTCUTS_CHANGED = 12, // as.document.id == 0 - the keyboard shortcut map was remapped.
    NIB_EV_SHUTDOWN_BEGIN = 13, // as.document.id == 0 - the application is beginning to close: a close was
                                //   requested but not yet committed, so a veto (an unsaved-changes prompt
                                //   cancelled) may still abort it -> NIB_EV_SHUTDOWN_CANCEL. It is NOT the
                                //   final teardown signal (a plugin unload/teardown still runs afterwards).
    NIB_EV_SHUTDOWN_CANCEL = 14, // as.document.id == 0 - a begun shutdown was cancelled; the app stays open.
    // ---- v4 additions (subscribe through the v4 table; new kinds, purely additive) ---------------
    // The file-lifecycle "long tail". Every one except NIB_EV_CMDLINE_PLUGIN_MSG carries as.document.id
    // (the affected buffer; 0 where none applies), the same shape as the DOCUMENT_* events - the id and
    // its path stay resolvable through nib.documents for the whole callback (the before/delete/rename
    // notifications fire before any tab teardown). A rename/delete BEFORE fires ahead of the operation and
    // is paired with a following RENAMED/DELETED (success) or RENAME_CANCEL/DELETE_FAILED (aborted).
    NIB_EV_DOCUMENT_BEFORE_LOAD = 15, // as.document.id == 0 - a file document is about to load its content
                                //   from disk (fired before the first byte is read; global, no id - a "before
                                //   load" signal, distinct from BEFORE_OPEN which already carries the id).
    NIB_EV_READONLY_CHANGED = 16, // as.document: id - the buffer's read-only state was toggled.
    NIB_EV_DOC_ORDER_CHANGED = 17, // as.document: id - the tab order changed (a tab drag or a Sort moved a
                                //   buffer); fires once per reorder, carrying the active buffer's id.
    NIB_EV_SNAPSHOT_DIRTY_LOADED = 18, // as.document: id - a dirty recovery-backup buffer was restored at startup.
    NIB_EV_FILE_BEFORE_RENAME = 19, // as.document: id - about to rename this buffer's on-disk file.
    NIB_EV_FILE_RENAME_CANCEL = 20, // as.document: id - a rename was cancelled or failed (pairs with BEFORE_RENAME).
    NIB_EV_FILE_RENAMED = 21,   // as.document: id - this buffer's on-disk file was renamed.
    NIB_EV_FILE_BEFORE_DELETE = 22, // as.document: id - about to delete this buffer's on-disk file.
    NIB_EV_FILE_DELETE_FAILED = 23, // as.document: id - a delete failed (pairs with BEFORE_DELETE).
    NIB_EV_FILE_DELETED = 24,   // as.document: id - this buffer's on-disk file was deleted (fired before the
                                //   buffer's tab is torn down, so the id/path stay resolvable in the callback).
    NIB_EV_CMDLINE_PLUGIN_MSG = 25 // as.cmdline: msg_utf8 - the host was launched with a -pluginMessage
                                //   command-line argument; msg_utf8 is that message (UTF-8), valid ONLY for
                                //   the duration of the callback. Fired once, after plugins are loaded.
} NibEventKind;
typedef struct NibEvent {
    NibEventKind kind;
    uint32_t     struct_size;
    union {
        struct { int64_t pos, added, removed; } text;
        struct { int64_t anchor, caret; }       selection;
        struct { intptr_t id; }                 document;
        // v3 NIB_EV_TEXT_MODIFIED: modification_type is the Scintilla SC_MOD_*/SC_PERFORMED_* bitset.
        struct { int64_t pos, length, lines_added; uint32_t modification_type; } modified;
        // v4 NIB_EV_CMDLINE_PLUGIN_MSG: msg_utf8 is host-owned and valid only during the callback.
        struct { const char* msg_utf8; }        cmdline;
    } as;
} NibEvent;
typedef void (*NibEventFn)(NibHost* host, const NibEvent* ev, void* user);
typedef struct NibEventsApi {
    uint32_t version;
    uint32_t struct_size;
    void (*subscribe)(NibHost*, NibEventKind kind, NibEventFn fn, void* user);  // call from activate()
    // ---- v3 (present only on the v3 table; guard with version >= 3) --------------------------------
    // Set the union of Scintilla modification_type (SC_MOD_*/SC_PERFORMED_*) flags for which the host
    // should fire NIB_EV_TEXT_MODIFIED. The default mask is 0, which disables the event entirely - the
    // host fires no modified event at all, so subscribing to NIB_EV_TEXT_MODIFIED costs nothing until at
    // least one flag is armed. A consumer that aggregates several opt-ins (e.g. the GPL npp-bridge
    // serving NPPM_ADDSCNMODIFIEDFLAGS) passes the running union each time. Idempotent; last call wins.
    void (*set_modified_mask)(NibHost*, uint32_t mask);
} NibEventsApi;

// ---- nib.langdef/1 : register a language definition ----------------------------------------------
// Lets a plugin add a highlightable language to the host. The host owns the highlighting engine
// (Scintillua - Lua LPeg lexers); a plugin supplies a lexer as Scintillua Lua source and the host
// loads it. This is how the optional GPL udl-compat plugin re-adds legacy Notepad++ User-Defined
// Languages after the core dropped its own UDL engine: it translates each userDefineLang.xml into a
// Scintillua lexer (see packages/udl-compat) and registers it here. Nothing here is Notepad++-shaped
// - it is a generic "here is a language and how to colour it" surface, useful to any Nib plugin.
#define NIB_IFACE_LANGDEF "nib.langdef/1"
typedef struct NibLangDefApi {
    uint32_t version;
    uint32_t struct_size;
    // Register a language. `name` is the display/menu name and the Scintillua lexer name; `extensions`
    // is a space-separated extension list without dots (e.g. "myl mylang"); `scintillua_lua` is the
    // lexer's Lua source (as produced by udl-compat's translateUdlToScintillua). The host copies all
    // three strings and compiles the Lua once. Returns 1 on success, 0 on failure (invalid Lua, empty
    // name, or a name already registered). Call from the plugin's activate().
    int (*register_language)(NibHost*, const char* name, const char* extensions,
                             const char* scintillua_lua);
} NibLangDefApi;

// ---- nib.keymap/1 : contribute keyboard bindings and named schemes -------------------------------
// Lets a plugin push keybinding overrides into the host's keymap store as a named, switchable SCHEME
// (a bundled default plus user/plugin schemes, each storing only deltas against a parent scheme). This
// is how the optional GPL npp-shortcuts-compat plugin re-adds Notepad++ shortcuts after parsing a
// shortcuts.xml: it translates each entry into a portable accelerator string and registers them here as
// a "Notepad++ (imported)" scheme. Nothing here is Notepad++-shaped - it is a generic "here is a key and
// what it should do" surface. Three binding namespaces mirror the host's three keymap tiers:
//   * command by id    - a frozen kCmd*/IDM_* number; translation-free for an importer that speaks them.
//   * command by name  - a stable symbolic name ("file.save", or a plugin command id "npp.mimeTools.4").
//   * editor by SCI id - a Scintilla SCI_* command, routed to the editor keymap (SCI_ASSIGNCMDKEY).
// Accelerator strings use the host's portable spelling ("Ctrl+Shift+S"); the host parses, validates, and
// OWNS a copy of every string, and maps Ctrl->Cmd itself on macOS. Only DATA crosses the boundary (never
// a live callback), so bindings are safe across plugin unload - unlike nib.events there is nothing left
// dangling. A committed scheme is written to the host's scheme store and OUTLIVES the plugin. All entry
// points are scheme-scoped: a plugin can never mutate the user's active scheme in place.
#define NIB_IFACE_KEYMAP "nib.keymap/1"
typedef struct NibKeymapScheme NibKeymapScheme;   // opaque host-side scheme-build handle

typedef struct NibKeymapApi {
    uint32_t version;
    uint32_t struct_size;

    // Begin building a named scheme. `id` is a stable ascii key ("org.wxnote.npp-imported"); `title` is
    // the user-visible name in the scheme picker; `parent` is the id this one deltas against (NULL/"" =>
    // host default; unknown id => host default, noted at commit). Committing over an existing id replaces
    // it (idempotent re-import). Returns an opaque handle, or NULL on failure. Invisible until commit.
    NibKeymapScheme* (*begin_scheme)(NibHost*, const char* id, const char* title, const char* parent);

    // Bind a host COMMAND by frozen numeric id (kCmd*/IDM_*). `accel` is a portable accelerator string,
    // or NULL/"" to UNBIND (mask the parent/default). `additional` non-zero ADDS `accel` as an extra
    // binding (N++ NextKey) instead of replacing. Returns 1, or 0 if the id is unknown or accel unparsable.
    int (*bind_id)(NibHost*, NibKeymapScheme*, int cmd_id, const char* accel, int additional);

    // Bind a host command by stable symbolic name. Same accel/unbind/additional semantics. Returns 1, or
    // 0 if the name is unknown or `accel` does not parse.
    int (*bind_name)(NibHost*, NibKeymapScheme*, const char* symbolic_name, const char* accel, int additional);

    // Bind an EDITOR command (Scintilla SCI_* id), routed to the editor keymap tier and applied to every
    // editor view. Same accel/unbind/additional semantics. Returns 1, or 0 if the SCI id or accel rejected.
    int (*bind_editor)(NibHost*, NibKeymapScheme*, int sci_command, const char* accel, int additional);

    // Abandon an uncommitted build and free the handle (e.g. on a parse error). No effect on any prior
    // committed scheme of the same id.
    void (*discard_scheme)(NibHost*, NibKeymapScheme*);

    // Commit: the host validates, writes the scheme to its store (PERSISTS across sessions, outlives this
    // plugin), and makes it selectable. The handle is consumed - do not reuse. `activate` non-zero also
    // switches the host to this scheme now (an importer typically passes 0). Returns 1, or 0 on failure.
    int (*commit_scheme)(NibHost*, NibKeymapScheme*, int activate);

    // ---- v2 (guard with version >= 2) : read the effective binding of a host command ---------------
    // Look up the EFFECTIVE keyboard shortcut currently bound to host command `cmd_id` (the primary
    // plain, non-chord accelerator - what shows on the menu). On a hit it writes a portable modifier
    // bitmask to *out_mods (bit0 = Ctrl, bit1 = Alt, bit2 = Shift; the host folds macOS Cmd into bit0)
    // and the accelerator's key code to *out_key (an uppercase ASCII code point for letters/digits/
    // punctuation, or a host key constant for a named key such as a function key), then returns 1.
    // Returns 0 when the command is unknown or unbound (out-params untouched). Read-only: it never
    // mutates the keymap, so it is safe to call at any time. A compat bridge maps the returned key code
    // into its own ABI's numbering (e.g. the Notepad++ ShortcutKey virtual-key code).
    int (*effective_shortcut)(NibHost*, int cmd_id, uint32_t* out_mods, uint32_t* out_key);
} NibKeymapApi;

// ---- nib.paths/1 : well-known host directories ---------------------------------------------------
// Gives a plugin the host's per-user data directory so it can find/read user assets (e.g. the
// udl-compat plugin reads userDefineLangs/ from here). Generic - no Notepad++ specifics.
#define NIB_IFACE_PATHS "nib.paths/1"
typedef struct NibPathsApi {
    uint32_t version;
    uint32_t struct_size;
    // Copy the per-user data dir (UTF-8, no trailing separator) into buf; returns byte length
    // excluding the NUL, or 0 if unavailable.
    int (*user_data_dir)(NibHost*, char* buf, int cap);
} NibPathsApi;

// ---- nib.panels/1 : dockable panels --------------------------------------------------------------
// A panel is a host-owned, dockable text view (the portable content model - the plugin pushes text,
// the host renders it via wxAui on every OS). Richer content models (canvas / webview) come later.
#define NIB_IFACE_PANELS "nib.panels/1"
typedef struct NibPanel NibPanel;   // opaque handle to a registered panel
typedef enum { NIB_DOCK_BOTTOM = 0, NIB_DOCK_LEFT, NIB_DOCK_RIGHT, NIB_DOCK_TOP } NibDock;
typedef struct NibPanelsApi {
    uint32_t   version;
    uint32_t   struct_size;
    NibPanel*  (*register_panel)(NibHost*, const char* id, const char* title, NibDock dock);  // NULL on failure
    void       (*set_text)(NibHost*, NibPanel*, const char* utf8);     // replace the panel's contents
    void       (*append_text)(NibHost*, NibPanel*, const char* utf8);  // append
    void       (*show)(NibHost*, NibPanel*, int visible);
} NibPanelsApi;

// ---- nib.win32/1 : Windows-only native-handle escape hatch (capability-gated) --------------------
// Offered only by the Windows host; query() returns NULL on Linux/macOS. The optional GPL npp-bridge
// uses it to rebuild the Notepad++ NppData environment for binary N++ plugins. Handles are void*
// (HWND/HMENU on Windows) so this header stays Win32-free.
#define NIB_IFACE_WIN32 "nib.win32/1"
typedef struct NibWin32Api {
    uint32_t version;
    uint32_t struct_size;
    void* (*main_window)(NibHost*);    // top-level frame HWND
    void* (*editor_main)(NibHost*);    // primary Scintilla editor HWND
    void* (*editor_second)(NibHost*);  // secondary editor HWND, or NULL if not split
    void* (*plugins_menu)(NibHost*);   // Plugins menu HMENU
    // Host a plugin-owned native child window in a host dock pane. edge: 0=bottom,1=left,2=right,3=top.
    // Starts hidden (call show_dock to reveal); calling again with the same hwnd is a no-op.
    void  (*dock_native)(NibHost*, void* hwnd, const char* title_utf8, int edge);
    void  (*show_dock)(NibHost*, void* hwnd, int visible);
} NibWin32Api;

// ---- nib.sci/1 : generic Scintilla passthrough ---------------------------------------------------
// A single, portable escape hatch to the host's editor(s): the plugin sends a raw Scintilla message
// and gets the raw result back. Unlike nib.win32 this carries no platform handles, so it is offered on
// EVERY OS - it is how the optional npp-bridge routes a recompiled Notepad++ plugin's SendMessage(SCI_*)
// into wxNote's wxStyledTextCtrl on Linux/macOS as well as Windows. The message numbers are Scintilla's
// own (SCI_*/Lexilla), which are cross-platform and not Notepad++-specific, so this stays clean-sheet.
#define NIB_IFACE_SCI "nib.sci/1"
typedef struct NibSciApi {
    uint32_t version;
    uint32_t struct_size;
    // Generic Scintilla passthrough. view: 0=main, 1=sub, -1=active. Returns the SCI result (0 if the
    // target view does not exist).
    intptr_t (*sci_call)(NibHost* host, int view, unsigned msg, uintptr_t wparam, intptr_t lparam);
} NibSciApi;

// ---- nib.ui/1 : host chrome state (menu checkmarks, dark mode, chrome visibility, editor state) --
// A small window into the host's own UI so a plugin can keep chrome in sync with its state: check/
// uncheck a menu item it registered, ask whether the host runs its dark chrome, and read the host's
// dark palette to paint its own surfaces to match. Colours are plain 0xRRGGBB - no COLORREF, no
// platform types - so the surface stays portable; any native conversion is the caller's business.
// The interface grows in place; guard the v2 fields with `version >= 2`:
//   * query(..., NIB_IFACE_UI, 1) or (..., 2) -> the same (v2) table; only the version field differs.
#define NIB_IFACE_UI "nib.ui/1"
typedef struct NibUiDarkColors {
    uint32_t version;            // caller sets the layout version it speaks (1)
    uint32_t struct_size;        // caller sets sizeof(NibUiDarkColors); lets this struct grow compatibly
    // The host's dark palette, 0xRRGGBB each. Meaningful pairings: text on background (bars/chrome),
    // text on pure_background (content areas), hot_background under the pointer, edge/hot_edge/
    // disabled_edge for control outlines.
    uint32_t background;         // chrome background (bars, panels)
    uint32_t softer_background;  // slightly raised surfaces (input rows, gutters)
    uint32_t hot_background;     // hover / hot-track fill
    uint32_t pure_background;    // content background (the editor's default style)
    uint32_t error_background;   // error field fill
    uint32_t text;               // primary text
    uint32_t darker_text;        // secondary text
    uint32_t disabled_text;      // disabled text
    uint32_t link_text;          // hyperlink text
    uint32_t edge;               // control outline
    uint32_t hot_edge;           // hovered control outline
    uint32_t disabled_edge;      // disabled control outline
} NibUiDarkColors;
typedef struct NibUiApi {
    uint32_t version;
    uint32_t struct_size;
    // Check/uncheck a checkable menu item by its numeric command id (a host kCmd* id or an id the
    // plugin's own commands were surfaced under). Returns 1 if the item exists and is checkable, else 0.
    int (*menu_check)(NibHost*, int cmd_id, int checked);
    // 1 when the host chrome runs dark, 0 when light. (Chrome darkness is fixed per process.)
    int (*is_dark)(NibHost*);
    // Fill `out` with the host's dark palette (see NibUiDarkColors). The caller must set out->version
    // and out->struct_size first; returns 1 on success, 0 if out is NULL or too small. The palette is
    // the host's DARK set regardless of is_dark() - callers normally consult is_dark() first.
    int (*dark_colors)(NibHost*, NibUiDarkColors* out);
    // ---- v2 (guard with version >= 2) : host chrome visibility + editor/UI state -------------------
    // A host chrome element's visibility. `which` selects it: 0 = toolbar, 1 = menubar, 2 = statusbar,
    // 3 = document-list panel. chrome_get returns 1 when the element is shown, 0 when hidden. chrome_set
    // shows (visible != 0) or hides it and returns the element's PREVIOUS visibility (1 shown, 0 hidden),
    // so a caller can report an old-state value. The MENUBAR (which == 1) is a portable, documented
    // NO-OP: where the platform owns a global menubar (macOS) it cannot be hidden, so - identically on
    // EVERY OS, to keep one portable visibility model rather than a per-platform detach hack - chrome_get
    // always reports it shown (1) and chrome_set leaves it shown and returns 1.
    int (*chrome_get)(NibHost*, int which);
    int (*chrome_set)(NibHost*, int which, int visible);
    // Line-number margin width policy: 0 = dynamic (the margin grows with the document's line count),
    // 1 = constant (a fixed width). width_mode_get returns the current policy; width_mode_set applies it
    // and returns 1, or 0 if `mode` is out of the 0..1 range.
    int (*width_mode_get)(NibHost*);
    int (*width_mode_set)(NibHost*, int mode);
    // 1 when the host auto-indents new lines, else 0.
    int (*auto_indent_on)(NibHost*);
    // The host's macro state: 0 = idle, 1 = recording in progress, 2 = a recorded macro is available to
    // play (recording stopped), 3 = playing back.
    int (*macro_state)(NibHost*);
    // The host's toolbar icon-set choice as a small non-negative integer (the host's own ordering).
    int (*icon_set_choice)(NibHost*);
    // Copy the host's active UI locale/language canonical name (e.g. "en", "pl", "ja") into buf
    // (NUL-terminated if it fits); returns the byte length excluding the NUL, or 0 if unavailable.
    int (*locale_name)(NibHost*, char* buf, int cap);
} NibUiApi;

// ---- nib.toolbar/1 : plugin toolbar buttons ------------------------------------------------------
// Adds a button to the host's main toolbar that fires a command id through the host's normal command
// dispatcher (the same wx-event path menu items use - NEVER a raw window message, so ids above 32767
// survive the 16-bit WM_COMMAND wrap intact). The icon crosses the boundary as portable RGBA pixels -
// no HBITMAP/HICON in the contract; a Windows compatibility bridge converts native handles to RGBA
// BEFORE calling in. The host copies the pixels and the tooltip immediately (nothing plugin-owned is
// retained) and rescales to its configured toolbar icon size; buttons are removed by the host on
// plugin unload, before the plugin unmaps.
#define NIB_IFACE_TOOLBAR "nib.toolbar/1"
typedef struct NibToolbarIcon {
    uint32_t version;      // caller sets 1
    uint32_t struct_size;  // caller sets sizeof(NibToolbarIcon)
    int32_t  width;        // pixels; > 0
    int32_t  height;       // pixels; > 0
    const unsigned char* rgba;  // width*height*4 bytes, RGBA order, row-major top-down, straight (non-premultiplied) alpha
} NibToolbarIcon;
typedef struct NibToolbarApi {
    uint32_t version;
    uint32_t struct_size;
    // Append one button. `cmd_id` is what clicking dispatches (a command registered via nib.commands,
    // or an id from nib.alloc alloc_cmd_ids). One button per id - a second add with the same id fails.
    // Returns 1 on success, 0 on failure (no toolbar, bad icon, or duplicate id).
    int (*add_tool)(NibHost*, int cmd_id, const NibToolbarIcon* icon, const char* tooltip_utf8);
} NibToolbarApi;

// ---- nib.alloc/1 : dynamic command-id / marker / indicator ranges --------------------------------
// Grants a plugin ranges of the shared number spaces so multiple plugins never collide with each
// other or with the host. Every grant is CONTIGUOUS ([first, first+count)) and process-lifetime
// (never recycled, even across plugin unload). The host guarantees each pool is disjoint from every
// number it uses itself; the concrete reserved ranges are documented at the host-side allocators.
//   * command ids: valid for nib.toolbar buttons, host menu items and invoke_command; when one fires,
//     the host calls every sink registered via on_command with that id (dispatch runs on the host's
//     wx-event command path, never through a raw 16-bit window message).
//   * markers / indicators: Scintilla marker / indicator numbers, usable directly via nib.sci.
#define NIB_IFACE_ALLOC "nib.alloc/1"
typedef void (*NibAllocCommandFn)(NibHost* host, int cmd_id, void* user);
typedef struct NibAllocApi {
    uint32_t version;
    uint32_t struct_size;
    // Each returns 1 and writes the first granted number to *first, or returns 0 (count <= 0, NULL
    // out-pointer, or the pool is exhausted - a failed call grants nothing).
    int  (*alloc_cmd_ids)(NibHost*, int count, int* first_id);
    int  (*alloc_markers)(NibHost*, int count, int* first_marker);
    int  (*alloc_indicators)(NibHost*, int count, int* first_indicator);
    // Register a sink for allocated command ids (call from activate()). Every sink hears every fired
    // allocated id - filter on cmd_id. Like nib.events subscriptions, sinks are dropped by the host on
    // plugin unload before the plugin unmaps; there is no unregister.
    void (*on_command)(NibHost*, NibAllocCommandFn fn, void* user);
} NibAllocApi;

// ---- nib.session/1 : save / load / enumerate session files -----------------------------------------
// A session is a set of open documents persisted as an XML file. This surface saves the host's open
// documents (or an explicit file list) to a session file, opens a session file's documents, and reads a
// session file's contents back - all BY PATH, so a plugin (or the GPL npp-bridge serving the Notepad++
// *SESSION* messages) drives it with no host dialog. The written XML is the host's session format: plain
// UTF-8 XML with a portable <Session>/<mainView>/<subView><File filename=...> shape that other tools
// that read that layout can parse.
#define NIB_IFACE_SESSION "nib.session/1"
typedef struct NibSessionApi {
    uint32_t version;
    uint32_t struct_size;
    // Save the host's currently-open (on-disk) documents as a session file at utf8_path. Returns 1 on
    // success, 0 on write failure.
    int (*save_current)(NibHost*, const char* utf8_path);
    // Save an explicit list of file paths (utf8_files[0..count), each UTF-8) as a session file at
    // utf8_path. Returns 1 on success, 0 on write failure or bad arguments.
    int (*save_files)(NibHost*, const char* utf8_path, const char* const* utf8_files, int count);
    // Open every file listed in the session file at utf8_path (each added as a document). Returns 1 if
    // the file parsed as a session (even if some listed files no longer exist), 0 if it could not be
    // read or is not a session file.
    int (*load)(NibHost*, const char* utf8_path);
    // Number of files listed in the session file at utf8_path (across all its views), or 0 if it cannot
    // be parsed. When out_valid is non-NULL it receives 1 if the file is a well-formed session XML, else 0.
    int (*file_count)(NibHost*, const char* utf8_path, int* out_valid);
    // Copy the UTF-8 path of the session's file at flat `index` (0-based, in document order across the
    // views) into buf (NUL-terminated if it fits); returns the byte length excluding the NUL, or 0 if
    // index is out of range or the file cannot be parsed.
    int (*file_at)(NibHost*, const char* utf8_path, int index, char* buf, int cap);
} NibSessionApi;

// ---- nib.lexer/1 : create a Scintilla lexer by name (Lexilla) --------------------------------------
// Hands a plugin a Scintilla ILexer instance built by the host's embedded Lexilla for a named lexer, so
// the plugin can drive syntax highlighting of a view via SCI_SETILEXER (through nib.sci). Portable:
// Lexilla is built into the host on every OS, so this works for a recompiled plugin everywhere - unlike a
// native lexer DLL. The lexer names are Lexilla's own (cross-platform), so nothing here is host-specific.
#define NIB_IFACE_LEXER "nib.lexer/1"
typedef struct NibLexerApi {
    uint32_t version;
    uint32_t struct_size;
    // Create an ILexer for the Lexilla lexer named lexer_name (e.g. "cpp", "python", "json"). Returns an
    // opaque Scintilla ILexer pointer as an intptr_t (apply it with SCI_SETILEXER via nib.sci; Scintilla
    // then owns it and Release()s it), or 0 if the name is unknown/empty. The caller owns the instance
    // until it hands it to a view.
    intptr_t (*create_lexer)(NibHost*, const char* lexer_name);
    // The number of plugin/user-registered custom languages (Scintillua lexers added via nib.langdef).
    int (*user_lang_count)(NibHost*);
} NibLexerApi;

// ---- the plugin's lifecycle vtable ---------------------------------------------------------------
typedef struct NibPluginApi {
    uint32_t    abi_version;                          // the ABI the plugin was built against
    uint32_t    struct_size;
    const char* id;                                   // e.g. "com.example.hello"
    void (*activate)(NibHost* host, NibQueryFn query); // register commands/panels/etc. here
    void (*deactivate)(NibHost* host);                 // optional teardown; may be NULL
} NibPluginApi;

// The one symbol every Nib plugin exports (the plugin defines it with NIB_API):
//   extern "C" NIB_API const NibPluginApi* nib_plugin_main(const NibBootstrap*);
// The host resolves it by name and calls it through this type.
typedef const NibPluginApi* (*NibPluginMainFn)(const NibBootstrap*);

#ifdef __cplusplus
}  // extern "C"
#endif
