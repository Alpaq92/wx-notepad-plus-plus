// SPDX-License-Identifier: Apache-2.0
//
// A minimal Nib plugin for wxNotepad++ - reference implementation + smoke test for the Nib plugin API.
// Cross-platform: it includes only nib.h (no Win32, no Notepad++ ABI) and is built as a shared module
// loaded from <exe>/nib/. It registers two commands that edit the active document through the API.
#include "nib.h"
#include <cstdio>

static void cmd_hello(NibHost* host, NibQueryFn query, void*)
{
    const NibEditorApi* ed = static_cast<const NibEditorApi*>(query(host, NIB_IFACE_EDITOR, 1));
    if (ed) ed->replace_selection(host, "// Hello from a Nib plugin - wxNotepad++'s own cross-platform API\n");
}

static void cmd_doclen(NibHost* host, NibQueryFn query, void*)
{
    const NibEditorApi* ed = static_cast<const NibEditorApi*>(query(host, NIB_IFACE_EDITOR, 1));
    if (!ed) return;
    char buf[80];
    std::snprintf(buf, sizeof(buf), "// document length = %lld bytes\n", static_cast<long long>(ed->length(host)));
    ed->replace_selection(host, buf);
}

static void activate(NibHost* host, NibQueryFn query)
{
    const NibCommandsApi* cmds = static_cast<const NibCommandsApi*>(query(host, NIB_IFACE_COMMANDS, 1));
    if (!cmds) return;
    cmds->register_command(host, "com.wxnpp.nibtest.hello",  "Insert Hello (Nib)",      cmd_hello,  nullptr);
    cmds->register_command(host, "com.wxnpp.nibtest.doclen", "Insert Doc Length (Nib)", cmd_doclen, nullptr);
}

static const NibPluginApi PLUGIN = {
    NIB_ABI_VERSION, sizeof(NibPluginApi), "com.wxnpp.nibtest", activate, /*deactivate*/ nullptr
};

// The single symbol the host resolves by name.
extern "C" NIB_API const NibPluginApi* nib_plugin_main(const NibBootstrap* boot)
{
    if (!boot || (boot->abi_version >> 16) != (NIB_ABI_VERSION >> 16)) return nullptr;  // host major must match
    return &PLUGIN;
}
