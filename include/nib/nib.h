// SPDX-License-Identifier: Apache-2.0
//
// Nib - the wxNote plugin API (codename "Nib", as in a pen nib).
// Copyright 2026 The wxNote Authors.
//
// An original, cross-platform, stable C ABI for extending wxNote. It is clean-sheet work: it
// borrows nothing from Notepad++ (no NPPM_* numbers, no FuncItem/NppData/SCNotification, no WM_USER,
// and no platform handles such as HWND in the contract). A plugin is a shared library exporting a
// single entry point; it asks the host for capability interfaces by stable id + version and gets back
// typed function tables. This file (and the API it defines) is permissively licensed even while the
// project as a whole ships GPL - it is the intended permissive core (see docs/FUTURE_PLANS.md).
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

#define NIB_ABI_VERSION 0x00010000u   // (major << 16) | minor  ->  1.0

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
} NibCommandsApi;

// ---- nib.events/1 : subscribe to editor / document events ----------------------------------------
#define NIB_IFACE_EVENTS "nib.events/1"
typedef enum {
    NIB_EV_TEXT_CHANGED = 1,    // as.text:      pos, added, removed (bytes)
    NIB_EV_SELECTION_CHANGED,   // as.selection: anchor, caret
    NIB_EV_DOCUMENT_SAVED,      // (no payload)
    NIB_EV_DOCUMENT_ACTIVATED   // as.document:   id (the now-active document's buffer id)
} NibEventKind;
typedef struct NibEvent {
    NibEventKind kind;
    uint32_t     struct_size;
    union {
        struct { int64_t pos, added, removed; } text;
        struct { int64_t anchor, caret; }       selection;
        struct { intptr_t id; }                 document;
    } as;
} NibEvent;
typedef void (*NibEventFn)(NibHost* host, const NibEvent* ev, void* user);
typedef struct NibEventsApi {
    uint32_t version;
    uint32_t struct_size;
    void (*subscribe)(NibHost*, NibEventKind kind, NibEventFn fn, void* user);  // call from activate()
} NibEventsApi;

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
