// SPDX-License-Identifier: Apache-2.0
//
// A minimal Nib plugin for wxNote - reference implementation + smoke test for the Nib plugin API.
// Cross-platform: it includes only nib.h (no Win32, no Notepad++ ABI) and is built as a shared module
// loaded from <exe>/nib/. It exercises four interfaces: nib.commands, nib.editor, nib.events, nib.panels.
#include "nib.h"
#include <cstdio>

// The bootstrap (host + query) is stashed at activate() so any callback can reach the host interfaces.
static NibHost*    g_host  = nullptr;
static NibQueryFn  g_query = nullptr;
static NibPanel*   g_panel = nullptr;
static int         g_textChanges = 0;

static void cmd_hello(NibHost* host, NibQueryFn query, void*)
{
    const NibEditorApi* ed = static_cast<const NibEditorApi*>(query(host, NIB_IFACE_EDITOR, 1));
    if (ed) ed->replace_selection(host, "// Hello from a Nib plugin - wxNote's own cross-platform API\n");
}

static void cmd_doclen(NibHost* host, NibQueryFn query, void*)
{
    const NibEditorApi* ed = static_cast<const NibEditorApi*>(query(host, NIB_IFACE_EDITOR, 1));
    if (!ed) return;
    char buf[80];
    std::snprintf(buf, sizeof(buf), "// document length = %lld bytes\n", static_cast<long long>(ed->length(host)));
    ed->replace_selection(host, buf);
}

static void cmd_evcount(NibHost* host, NibQueryFn query, void*)
{
    const NibEditorApi* ed = static_cast<const NibEditorApi*>(query(host, NIB_IFACE_EDITOR, 1));
    if (!ed) return;
    char buf[96];
    std::snprintf(buf, sizeof(buf), "// Nib text-changed events seen: %d\n", g_textChanges);
    ed->replace_selection(host, buf);
}

// Probe the Windows-only nib.win32 capability (it is the gate the GPL N++ bridge will use).
static void cmd_win32(NibHost* host, NibQueryFn query, void*)
{
    const NibEditorApi* ed = static_cast<const NibEditorApi*>(query(host, NIB_IFACE_EDITOR, 1));
    if (!ed) return;
    const NibWin32Api* w = static_cast<const NibWin32Api*>(query(host, NIB_IFACE_WIN32, 1));
    char buf[128];
    if (w) std::snprintf(buf, sizeof(buf), "// nib.win32 available: frame=%p editor=%p menu=%p\n",
                         w->main_window(host), w->editor_main(host), w->plugins_menu(host));
    else   std::snprintf(buf, sizeof(buf), "// nib.win32 not offered on this platform (native plugins only)\n");
    ed->replace_selection(host, buf);
}

// On every text change, append a line to our docked panel (the nib.panels demo).
static void on_text_changed(NibHost*, const NibEvent* ev, void*)
{
    ++g_textChanges;
    if (!g_panel || !g_query) return;
    const NibPanelsApi* panels = static_cast<const NibPanelsApi*>(g_query(g_host, NIB_IFACE_PANELS, 1));
    if (!panels) return;
    char line[96];
    std::snprintf(line, sizeof(line), "text changed @ %lld  (+%lld -%lld)\n",
                  static_cast<long long>(ev->as.text.pos),
                  static_cast<long long>(ev->as.text.added),
                  static_cast<long long>(ev->as.text.removed));
    panels->append_text(g_host, g_panel, line);
}

static void activate(NibHost* host, NibQueryFn query)
{
    g_host = host; g_query = query;

    const NibCommandsApi* cmds = static_cast<const NibCommandsApi*>(query(host, NIB_IFACE_COMMANDS, 1));
    if (cmds) {
        cmds->register_command(host, "com.wxn.nibtest.hello",   "Insert Hello (Nib)",      cmd_hello,   nullptr);
        cmds->register_command(host, "com.wxn.nibtest.doclen",  "Insert Doc Length (Nib)", cmd_doclen,  nullptr);
        cmds->register_command(host, "com.wxn.nibtest.evcount", "Show Nib Event Count",    cmd_evcount, nullptr);
        cmds->register_command(host, "com.wxn.nibtest.win32",   "Check nib.win32 (handles)", cmd_win32, nullptr);
    }

    const NibPanelsApi* panels = static_cast<const NibPanelsApi*>(query(host, NIB_IFACE_PANELS, 1));
    if (panels) {
        g_panel = panels->register_panel(host, "com.wxn.nibtest.log", "Nib Events Log", NIB_DOCK_BOTTOM);
        if (g_panel) panels->set_text(host, g_panel, "Nib Events Log - type in the editor to see text-changed events.\n");
    }

    const NibEventsApi* events = static_cast<const NibEventsApi*>(query(host, NIB_IFACE_EVENTS, 1));
    if (events) events->subscribe(host, NIB_EV_TEXT_CHANGED, on_text_changed, nullptr);
}

static const NibPluginApi PLUGIN = {
    NIB_ABI_VERSION, sizeof(NibPluginApi), "com.wxn.nibtest", activate, /*deactivate*/ nullptr
};

// The single symbol the host resolves by name.
extern "C" NIB_API const NibPluginApi* nib_plugin_main(const NibBootstrap* boot)
{
    if (!boot || (boot->abi_version >> 16) != (NIB_ABI_VERSION >> 16)) return nullptr;  // host major must match
    return &PLUGIN;
}
