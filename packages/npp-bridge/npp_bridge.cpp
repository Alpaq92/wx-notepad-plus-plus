// SPDX-License-Identifier: GPL-3.0-or-later
//
// npp-bridge - the optional Notepad++ binary-plugin compatibility bridge for wxNotepad++.
//
// It is a Nib plugin (Windows-only) that reaches the host's native handles through the nib.win32
// capability and rebuilds the Notepad++ NppData environment, so real Notepad++ plugin DLLs can be
// hosted. This module is GPL because it reproduces Notepad++'s plugin ABI; the wxNotepad++ core does
// NOT depend on it - it is loaded, like any plugin, only if present. Keeping it separate + GPL is what
// lets the core relicense permissively (see docs/FUTURE_PLANS.md).
//
// STAGE 1: stand up the package and prove the bridge can reconstruct NppData from nib.win32. Loading
// the N++ DLLs (Stage 2) and routing NPPM_*/SCI (Stage 3) follow; then the core's host is removed.
#include "nib.h"
#include "PluginInterface.h"   // NppData - the Notepad++ ABI this bridge targets
#include <cstdio>

static NppData g_npp = {};   // the Notepad++ environment, rebuilt from nib.win32

// A temporary diagnostic command (Stage 1): report the NppData we reconstructed.
static void cmd_info(NibHost* host, NibQueryFn query, void*)
{
    const NibEditorApi* ed = static_cast<const NibEditorApi*>(query(host, NIB_IFACE_EDITOR, 1));
    if (!ed) return;
    char buf[176];
    std::snprintf(buf, sizeof(buf),
        "// npp-bridge: NppData rebuilt from nib.win32 - npp=%p sciMain=%p sciSecond=%p\n",
        static_cast<void*>(g_npp._nppHandle),
        static_cast<void*>(g_npp._scintillaMainHandle),
        static_cast<void*>(g_npp._scintillaSecondHandle));
    ed->replace_selection(host, buf);
}

static void activate(NibHost* host, NibQueryFn query)
{
    // Reach the host's native handles through the capability-gated nib.win32 interface.
    const NibWin32Api* w = static_cast<const NibWin32Api*>(query(host, NIB_IFACE_WIN32, 1));
    if (w) {
        g_npp._nppHandle             = static_cast<HWND>(w->main_window(host));
        g_npp._scintillaMainHandle   = static_cast<HWND>(w->editor_main(host));
        g_npp._scintillaSecondHandle = static_cast<HWND>(w->editor_second(host));
    }
    const NibCommandsApi* cmds = static_cast<const NibCommandsApi*>(query(host, NIB_IFACE_COMMANDS, 1));
    if (cmds) cmds->register_command(host, "org.wxnpp.npp-bridge.info", "N++ Bridge: NppData Info", cmd_info, nullptr);
}

static const NibPluginApi PLUGIN = {
    NIB_ABI_VERSION, sizeof(NibPluginApi), "org.wxnpp.npp-bridge", activate, /*deactivate*/ nullptr
};

extern "C" NIB_API const NibPluginApi* nib_plugin_main(const NibBootstrap* boot)
{
    if (!boot || (boot->abi_version >> 16) != (NIB_ABI_VERSION >> 16)) return nullptr;
    return &PLUGIN;
}
