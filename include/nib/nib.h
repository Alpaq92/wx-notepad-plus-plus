// SPDX-License-Identifier: Apache-2.0
//
// Nib - the wxNotepad++ plugin API (codename "Nib", as in a pen nib).
// Copyright 2026 The wxNotepad++ Authors.
//
// An original, cross-platform, stable C ABI for extending wxNotepad++. It is clean-sheet work: it
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
    const char* (*product_name)(NibHost*);   // e.g. "wxNotepad++"
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
    NIB_EV_DOCUMENT_SAVED       // (no payload)
} NibEventKind;
typedef struct NibEvent {
    NibEventKind kind;
    uint32_t     struct_size;
    union {
        struct { int64_t pos, added, removed; } text;
        struct { int64_t anchor, caret; }       selection;
    } as;
} NibEvent;
typedef void (*NibEventFn)(NibHost* host, const NibEvent* ev, void* user);
typedef struct NibEventsApi {
    uint32_t version;
    uint32_t struct_size;
    void (*subscribe)(NibHost*, NibEventKind kind, NibEventFn fn, void* user);  // call from activate()
} NibEventsApi;

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
